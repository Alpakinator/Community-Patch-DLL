// art_files.cpp - FileProvider, DirectoryProvider (epic 3100, issue 3104). Own code, GPL-3.0-or-later.
#include "c5models/art.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace c5m {

std::string ascii_lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return r;
}

std::string basename_lower(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    std::string b = p == std::string::npos ? path : path.substr(p + 1);
    // trim surrounding white space (table values are sometimes padded)
    size_t a = b.find_first_not_of(" \t\r\n");
    size_t e = b.find_last_not_of(" \t\r\n");
    b = a == std::string::npos ? std::string() : b.substr(a, e - a + 1);
    return ascii_lower(b);
}

bool FileProvider::exists(const std::string& name, std::string* origin) {
    Bytes b;
    return read(name, b, origin);
}

std::vector<std::string> FileProvider::duplicates(const std::string&) const { return {}; }

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool read_wide(const std::wstring& path, Bytes& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

struct Found {
    std::wstring rel;   // relative path, backslashes
    uint64_t size;
};

// Sorted (case-insensitive by name) recursive walk; files of a folder before its subfolders.
void walk(const std::wstring& root, const std::wstring& rel, std::vector<Found>& out) {
    std::wstring pattern = root + (rel.empty() ? L"" : L"\\" + rel) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::pair<std::wstring, uint64_t>> files;
    std::vector<std::wstring> dirs;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // never follow links
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            dirs.push_back(n);
        else
            files.emplace_back(n, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    auto ci_less = [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; };
    std::sort(files.begin(), files.end(),
              [&](const std::pair<std::wstring, uint64_t>& a, const std::pair<std::wstring, uint64_t>& b) {
                  return ci_less(a.first, b.first);
              });
    std::sort(dirs.begin(), dirs.end(), ci_less);
    for (auto& f : files) out.push_back({rel.empty() ? f.first : rel + L"\\" + f.first, f.second});
    for (auto& d : dirs) walk(root, rel.empty() ? d : rel + L"\\" + d, out);
}

}  // namespace

bool read_whole_file(const std::string& utf8_path, Bytes& out) { return read_wide(widen(utf8_path), out); }

bool DirectoryProvider::add_root(const std::string& tag, const std::string& dir, Error* err) {
    std::wstring w = widen(dir);
    while (!w.empty() && (w.back() == L'/' || w.back() == L'\\')) w.pop_back();
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
        if (err) *err = {Err::NotFound, "root folder not found: " + dir};
        return false;
    }
    std::vector<Found> found;
    walk(w, L"", found);
    for (auto& f : found) {
        Entry e;
        std::string rel = narrow(f.rel);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        e.name = basename_lower(rel);
        e.origin = tag + ":" + rel;
        e.path = w + L"\\" + f.rel;
        e.size = f.size;
        index_[e.name].push_back(entries_.size());
        entries_.push_back(std::move(e));
    }
    ++roots_;
    return true;
}

bool DirectoryProvider::exists(const std::string& name, std::string* origin) {
    auto it = index_.find(name);
    if (it == index_.end()) return false;
    if (origin) *origin = entries_[it->second.front()].origin;
    return true;
}

bool DirectoryProvider::read(const std::string& name, Bytes& out, std::string* origin) {
    auto it = index_.find(name);
    if (it == index_.end()) return false;
    const Entry& e = entries_[it->second.front()];
    if (origin) *origin = e.origin;
    return read_wide(e.path, out);
}

bool DirectoryProvider::read_entry(const Entry& e, Bytes& out) const { return read_wide(e.path, out); }

std::vector<std::string> DirectoryProvider::duplicates(const std::string& name) const {
    std::vector<std::string> r;
    auto it = index_.find(name);
    if (it != index_.end())
        for (size_t i : it->second) r.push_back(entries_[i].origin);
    return r;
}

}  // namespace c5m
