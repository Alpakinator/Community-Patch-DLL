// units_mods.h - issue 0443: the mod roots of the install the units tests read (C5R_GAME_DIR), so that VP's models
// are found when VP is installed as mods in MODS (the pool instances) and not as a DLC modpack (the default copy).
//
// Rule, first that applies:
//   1. C5R_UNITS_MODS set and not empty: its folders, joined by ';', in that order (the rule before 0443).
//   2. The install is a pool instance (tools/instances/provision.py): <install>/../instance.json exists. Its MODS folder
//      is <instance>/<user_dir>/MODS, user_dir from instance.json (else "Documents\My Games\Sid Meier's Civilization 5");
//      every folder of MODS, sorted by name, is a root (the server's rule when the activated mods are not known,
//      srv_residency_source.cpp ModFolders / EveryFolder; files beside the folders, such as a .zip, are not).
//   3. None: the install's own files (VP as DLC there).
// The user's own Documents folder is never read: a test of an install copy must not depend on the workstation's mods.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"

namespace units_test {

// The parent of a folder path ("" when it has none); trailing separators ignored.
inline std::string parentDir(std::string p) {
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s);
}

// The string value of a top-level key of a small JSON file ("" when absent); handles \\, \" and \/ only.
inline std::string jsonString(const std::string& text, const std::string& key) {
    size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return std::string();
    k = text.find(':', k + key.size() + 2);
    if (k == std::string::npos) return std::string();
    k = text.find('"', k);
    if (k == std::string::npos) return std::string();
    std::string v;
    for (size_t i = k + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') return v;
        if (c == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            v += (n == '\\' || n == '"' || n == '/') ? n : '?';
        } else {
            v += c;
        }
    }
    return std::string();   // unterminated
}

// Every folder directly under dir (UTF-8 paths), sorted case-insensitively; empty when dir is not a folder.
inline std::vector<std::string> subFolders(const std::string& dir) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(c5r::gamefs::fromUtf8(dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::vector<std::string>();
    do {
        const std::wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L"..") names.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    std::vector<std::string> out;
    for (const std::wstring& n : names) out.push_back(dir + "\\" + c5r::gamefs::toUtf8(n));
    return out;
}

// The rule above for an install folder and a C5R_UNITS_MODS value; *how names the branch taken (for the test's log).
inline std::vector<std::string> modRootsFor(const std::string& gameDir, const std::string& envMods, std::string* how) {
    std::vector<std::string> out;
    if (!envMods.empty()) {
        std::stringstream ss(envMods);
        std::string m;
        while (std::getline(ss, m, ';'))
            if (!m.empty()) out.push_back(m);
        if (how) *how = "C5R_UNITS_MODS";
        return out;
    }
    const std::string inst = parentDir(gameDir);
    if (!inst.empty()) {
        std::ifstream f(c5r::gamefs::fromUtf8(inst + "\\instance.json").c_str(), std::ios::binary);
        if (f) {
            std::stringstream text;
            text << f.rdbuf();
            std::string user = jsonString(text.str(), "user_dir");
            if (user.empty()) user = "Documents\\My Games\\Sid Meier's Civilization 5";
            const std::string mods = inst + "\\" + user + "\\MODS";
            out = subFolders(mods);
            if (how) *how = "pool instance: " + mods;
            return out;
        }
    }
    if (how) *how = "none (no C5R_UNITS_MODS, no instance.json beside the install)";
    return out;
}

// For the install the tests read (C5R_GAME_DIR): the roots as GameFiles::Options::extraLooseRoots, printed once.
inline std::vector<std::wstring> installModRoots(const std::string& gameDir) {
    const char* e = std::getenv("C5R_UNITS_MODS");
    std::string how;
    const std::vector<std::string> roots = modRootsFor(gameDir, e ? e : "", &how);
    std::printf("mod roots (%s): %zu\n", how.c_str(), roots.size());
    std::vector<std::wstring> out;
    for (const std::string& r : roots) {
        std::printf("  %s\n", r.c_str());
        out.push_back(c5r::gamefs::fromUtf8(r));
    }
    return out;
}

}  // namespace units_test
