// render/src/gamefs/gamefiles.cpp - the game's virtual file system rebuilt from an install (task 2901). Own code; the
// order and the replace rule follow the EXE as described in gamefiles.h and asset-pipeline.md 1.2.
#include "c5r/gamefs/gamefiles.h"
#include <algorithm>
#include <windows.h>

namespace c5r { namespace gamefs {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

const std::vector<std::string>& baseDx9Packs() {
    static const std::vector<std::string> v = {
        "AmericanWashingtonTextures", "ArabiaHarunAl-RashidTextures", "AztecMontezumaTextures", "BuildingTextures",
        "ChineseWuTextures", "CityTextures", "DecalTextures", "EffectTextures", "EgyptianRamessesTextures",
        "EnglandElizabethTextures", "FeatureTextures", "FrenchNapoleonTextures", "GermanBismarckTextures",
        "GreeceAlexanderTextures", "HutsBarbarianFortTextures", "ImprovementTextures", "IndianGandhiTextures",
        "IroquoisHiawathaTextures", "JapaneseOdaNobunugaTextures", "LeadersTextures", "MiscTextures",
        "OttomanSuliemanTextures", "PersiaDariusTextures", "RomanCaesarTextures", "RussianCatherineTextures",
        "SiamRamkhamaengTextures", "SonghaiAskiaTextures", "StrategicViewTextures", "TerrainTextures", "UITextures",
        "UnitsTextures", "WonderTextures"};
    return v;
}

const std::vector<std::string>& baseCommonPacks() {
    static const std::vector<std::string> v = {
        "BuildingModels", "CityModels", "EffectData", "FeatureModels", "HutsBarbarianFortModels", "ImprovementModels",
        "LeaderModels", "MiscData", "SoundData", "TempModels", "TerrainModels", "UnitModels", "WonderModels"};
    return v;
}

const std::vector<std::string>& baseDx9LowPacks() {
    static const std::vector<std::string> v = {
        "AmericanWashingtonTextures", "ArabiaHarunAl-RashidTextures", "AztecMontezumaTextures", "ChineseWuTextures",
        "CityTextures", "EgyptianRamessesTextures", "EnglandElizabethTextures", "FrenchNapoleonTextures",
        "GermanBismarckTextures", "GreeceAlexanderTextures", "IndianGandhiTextures", "IroquoisHiawathaTextures",
        "JapaneseOdaNobunugaTextures", "MiscTextures", "OttomanSuliemanTextures", "PersiaDariusTextures",
        "RomanCaesarTextures", "RussianCatherineTextures", "SiamRamkhamaengTextures", "SonghaiAskiaTextures",
        "TerrainTextures", "UITextures"};
    return v;
}

namespace {

bool isDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool isFile(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring lowerW(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return s;
}
std::wstring slashes(std::wstring s) {
    for (wchar_t& c : s) {
        if (c == L'\\') c = L'/';
    }
    while (!s.empty() && s.back() == L'/') s.pop_back();
    return s;
}
// "Expansion2UITextures.fpk" or "x/y/Name" -> "Name" without extension.
std::string packBase(const std::string& f) {
    std::string s = f;
    size_t sl = s.find_last_of("/\\");
    if (sl != std::string::npos) s = s.substr(sl + 1);
    if (s.size() > 4 && lowerAscii(s.substr(s.size() - 4)) == ".fpk") s.resize(s.size() - 4);
    return s;
}
Status readWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Status::fail(Err::Io, "cannot open " + toUtf8(path));
    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li) || li.QuadPart > 0x7fffffff) {
        CloseHandle(f);
        return Status::fail(Err::Io, "cannot size " + toUtf8(path));
    }
    out.resize(static_cast<size_t>(li.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 30));
        if (!ReadFile(f, out.data() + done, want, &got, nullptr) || got == 0) {
            CloseHandle(f);
            out.clear();
            return Status::fail(Err::Io, "cannot read " + toUtf8(path));
        }
        done += got;
    }
    CloseHandle(f);
    return Status::Ok();
}

} // namespace

GameFiles::GameFiles() {}
GameFiles::~GameFiles() {}
Status GameFiles::open(const std::wstring& installRoot) { return open(installRoot, Options()); }

bool GameFiles::replaces(uint32_t flags, bool existingLoose) {
    if ((flags & 1) && existingLoose) return true;
    uint32_t m = flags & 0xE;
    if (m == 0) return false;
    if (m == 4 || m == 6) return !existingLoose;
    return true;
}

void GameFiles::registerName(Layer& L, const std::string& name, const Ref& r, uint32_t flags) {
    auto it = L.map.find(name);
    if (it == L.map.end()) {
        L.map.emplace(name, r);
        return;
    }
    if (replaces(flags, it->second.archive < 0)) {
        if (opt_.keepShadowed) L.shadowed[name].push_back(it->second);
        it->second = r;
    } else if (opt_.keepShadowed) {
        L.shadowed[name].push_back(r);
    }
}

int GameFiles::mountPack(Layer& L, const std::wstring& path, uint32_t flags) {
    std::wstring key = lowerW(slashes(path));
    for (size_t i = 0; i < mountedPaths_.size(); ++i) {
        if (lowerW(slashes(mountedPaths_[i])) == key) return -1;  // already mounted: the EXE finds it by name and skips
    }
    std::unique_ptr<FpkArchive> a(new FpkArchive);
    Status st = a->open(path);
    if (!st.ok()) {
        warnings_.push_back("pack " + toUtf8(path) + ": " + st.str());
        return -1;
    }
    int idx = static_cast<int>(archives_.size());
    const auto& ents = a->entries();
    archives_.push_back(std::move(a));
    mountedPaths_.push_back(path);
    for (size_t i = 0; i < ents.size(); ++i) {
        Ref r;
        r.archive = idx;
        r.index = static_cast<uint32_t>(i);
        r.size = ents[i].size;
        registerName(L, lowerAscii(ents[i].name), r, flags);
    }
    return idx;
}

void GameFiles::scanLoose(Layer& L, const std::wstring& dir, uint32_t flags, bool baseAssets, bool packageDir,
                          const std::vector<std::wstring>* excludeDirs) {
    std::vector<std::wstring> stack{dir};
    while (!stack.empty()) {
        std::wstring d = stack.back();
        stack.pop_back();
        if (excludeDirs) {
            const std::wstring key = lowerW(slashes(d));
            if (std::find(excludeDirs->begin(), excludeDirs->end(), key) != excludeDirs->end()) continue;
        }
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::vector<std::wstring> subdirs, files;
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring ln = lowerW(n);
                if (baseAssets && ln == L"dlc") continue;          // "*\DLC\*" excluded from the base Assets scan
                if (packageDir && ln == L"scenarios") continue;    // "*\Scenarios\*"
                if (packageDir && ln == L"speech" && lowerW(d).size() >= 7 &&
                    lowerW(d).compare(lowerW(d).size() - 7, 7, L"\\sounds") == 0)
                    continue;                                      // "*\Sounds\Speech\*"
                subdirs.push_back(d + L"\\" + n);
            } else {
                files.push_back(n);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(files.begin(), files.end());
        for (const std::wstring& n : files) {
            Ref r;
            r.archive = -1;
            r.index = static_cast<uint32_t>(loosePaths_.size());
            WIN32_FILE_ATTRIBUTE_DATA ad;
            std::wstring full = d + L"\\" + n;
            if (GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &ad))
                r.size = (static_cast<uint64_t>(ad.nFileSizeHigh) << 32) | ad.nFileSizeLow;
            loosePaths_.push_back(full);
            registerName(L, lowerAscii(toUtf8(n)), r, flags);
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) stack.push_back(*it);
    }
}

Status GameFiles::open(const std::wstring& installRoot, const Options& opt) {
    opt_ = opt;
    root_ = installRoot;
    while (!root_.empty() && (root_.back() == L'\\' || root_.back() == L'/')) root_.pop_back();
    archives_.clear();
    mountedPaths_.clear();
    loosePaths_.clear();
    layers_.clear();
    final_.clear();
    packages_.clear();
    warnings_.clear();
    const std::wstring assets = root_ + L"\\Assets", res = root_ + L"\\Resource";
    if (!isDir(assets) || !isDir(res)) return Status::fail(Err::Io, "gamefiles: no Assets/ or Resource/ under " + toUtf8(root_));

    Layer extra;
    extra.label = "extra";
    for (const std::wstring& r : opt.extraLooseRoots) {
        if (!isDir(r)) {
            warnings_.push_back("extra root missing: " + toUtf8(r));
            continue;
        }
        scanLoose(extra, r, 0, false, false);  // flags 0: an earlier root keeps its entry
    }

    // Packages, ascending (Priority, folder name).
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((assets + L"\\DLC\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::wstring folder = assets + L"\\DLC\\" + fd.cFileName;
            WIN32_FIND_DATAW pf;
            HANDLE h2 = FindFirstFileW((folder + L"\\*.civ5pkg").c_str(), &pf);
            if (h2 == INVALID_HANDLE_VALUE) continue;
            do {
                std::wstring file = folder + L"\\" + pf.cFileName;
                std::vector<uint8_t> bytes;
                Civ5Pkg pkg;
                Status st = readWholeFile(file, bytes);
                if (st.ok()) st = parseCiv5Pkg(reinterpret_cast<const char*>(bytes.data()), bytes.size(), pkg);
                if (!st.ok()) {
                    warnings_.push_back("package " + toUtf8(file) + ": " + st.str());
                    continue;
                }
                pkg.folder = folder;
                pkg.file = file;
                packages_.push_back(std::move(pkg));
            } while (FindNextFileW(h2, &pf));
            FindClose(h2);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::stable_sort(packages_.begin(), packages_.end(), [](const Civ5Pkg& a, const Civ5Pkg& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        std::wstring fa = lowerW(a.folder), fb = lowerW(b.folder);
        if (fa != fb) return fa < fb;
        return lowerW(a.file) < lowerW(b.file);
    });
    Layer pk;
    pk.label = "packages";
    auto skinMatches = [&](const Civ5PkgSkin& s) { return s.set == opt.skinSet && s.platform == opt.platform; };
    for (const Civ5Pkg& p : packages_) {
        std::vector<std::string> paks = p.paks;
        for (const Civ5PkgSkin& s : p.skins) {
            if (!skinMatches(s)) continue;
            paks.insert(paks.end(), s.skinPaks.begin(), s.skinPaks.end());
            paks.insert(paks.end(), s.gameplaySkinPaks.begin(), s.gameplaySkinPaks.end());
        }
        for (const std::string& f : paks) {
            std::wstring b = fromUtf8(packBase(f));
            std::wstring dx9 = res + L"\\DX9\\" + b + L".fpk", common = res + L"\\Common\\" + b + L".fpk";
            if (isFile(dx9)) mountPack(pk, dx9, 4);
            else if (isFile(common)) mountPack(pk, common, 4);
            else warnings_.push_back("package " + toUtf8(p.file) + " names pack " + f + ", not found");
            if (opt.includeDx9Low) {
                std::wstring low = res + L"\\DX9_Low\\" + b + L"DX9Low.fpk";
                if (isFile(low)) mountPack(pk, low, 4);
            }
        }
    }
    // The "Common" kind's loose files (issue 3404, asset-pipeline.md 1.2): 0x630E50 with kind 0 collects no Directory
    // and registers the package folder itself with flags 7, excluding *\Sounds\Speech\*, *\Scenarios\* and every
    // directory the package names for the other kinds (Gameplay Directory / MapDirectory, every UISkin's Skin and
    // GameplaySkin Directory, active or not). This is how Assets/DLC/Shared/Terrain/TerrainDef.csv (Upgrade1.Civ5Pkg
    // names no Directory) replaces the base pack's older terraindef.csv.
    if (opt.mountPackageRoots) {
        // One exclusion list over all packages (the EXE collects the other kinds' directories across packages), so that
        // packages sharing a folder (UI_bc1: EUI_0/1/2, one skin each) keep every skin's directories out.
        std::vector<std::wstring> excl, folders;
        for (const Civ5Pkg& p : packages_) {
            auto addExcl = [&](const std::string& d) {
                std::wstring w = fromUtf8(d);
                for (wchar_t& c : w) {
                    if (c == L'/') c = L'\\';
                }
                excl.push_back(lowerW(slashes(p.folder + L"\\" + w)));
            };
            for (const std::string& d : p.gameplayDirs) addExcl(d);
            for (const std::string& d : p.gameplayMapDirs) addExcl(d);
            for (const Civ5PkgSkin& s : p.skins) {
                for (const std::string& d : s.skinDirs) addExcl(d);
                for (const std::string& d : s.gameplaySkinDirs) addExcl(d);
            }
            const std::wstring key = lowerW(slashes(p.folder));
            bool seen = false;
            for (const std::wstring& f : folders) seen |= lowerW(slashes(f)) == key;
            if (!seen) folders.push_back(p.folder);
        }
        for (const std::wstring& f : folders)
            if (isDir(f)) scanLoose(pk, f, 7, false, true, &excl);
    }
    for (const Civ5Pkg& p : packages_) {
        std::vector<std::string> dirs = p.gameplayDirs;
        dirs.insert(dirs.end(), p.gameplayMapDirs.begin(), p.gameplayMapDirs.end());
        for (const Civ5PkgSkin& s : p.skins) {
            if (!skinMatches(s)) continue;
            dirs.insert(dirs.end(), s.skinDirs.begin(), s.skinDirs.end());
            dirs.insert(dirs.end(), s.gameplaySkinDirs.begin(), s.gameplaySkinDirs.end());
        }
        for (const std::string& d : dirs) {
            std::wstring w = fromUtf8(d);
            for (wchar_t& c : w) {
                if (c == L'/') c = L'\\';
            }
            std::wstring full = p.folder + L"\\" + w;
            if (isDir(full)) scanLoose(pk, full, 7, false, true);
        }
    }

    Layer low;
    low.label = "dx9low";
    if (opt.includeDx9Low) {
        for (const std::string& n : baseDx9LowPacks()) {
            std::wstring path = res + L"\\DX9_Low\\" + fromUtf8(n) + L"DX9Low.fpk";
            if (isFile(path)) mountPack(low, path, 4);
            else warnings_.push_back("base DX9_Low pack missing: " + n);
        }
    }

    Layer base;
    base.label = "base";
    for (const std::string& n : baseDx9Packs()) {
        std::wstring path = res + L"\\DX9\\" + fromUtf8(n) + L".fpk";
        if (isFile(path)) mountPack(base, path, 0);
        else warnings_.push_back("base DX9 pack missing: " + n);
    }
    for (const std::string& n : baseCommonPacks()) {
        std::wstring path = res + L"\\Common\\" + fromUtf8(n) + L".fpk";
        if (isFile(path)) mountPack(base, path, 0);
        else warnings_.push_back("base Common pack missing: " + n);
    }
    if (isDir(res + L"\\Overrides")) scanLoose(base, res + L"\\Overrides", 4, false, false);
    scanLoose(base, assets, opt.looseOverridesPak ? 6u : 1u, true, false);

    if (opt.extraLooseFirst) layers_.push_back(std::move(extra));
    layers_.push_back(std::move(pk));
    layers_.push_back(std::move(low));
    layers_.push_back(std::move(base));
    if (!opt.extraLooseFirst) layers_.push_back(std::move(extra));

    for (int li = static_cast<int>(layers_.size()) - 1; li >= 0; --li) {
        for (const auto& kv : layers_[li].map) final_[kv.first] = std::make_pair(li, kv.second);
    }
    return Status::Ok();
}

const GameFiles::Ref* GameFiles::lookup(const std::string& name) const {
    std::string key = lowerAscii(name);
    size_t sl = key.find_last_of("/\\");
    if (sl != std::string::npos) key = key.substr(sl + 1);  // the engine resolves by bare file name
    auto it = final_.find(key);
    return it == final_.end() ? nullptr : &it->second.second;
}

std::string GameFiles::originOf(const Ref& r) const {
    std::wstring p = r.archive >= 0 ? mountedPaths_[static_cast<size_t>(r.archive)] : loosePaths_[r.index];
    std::wstring rel = p;
    if (lowerW(p).compare(0, root_.size() + 1, lowerW(root_) + L"\\") == 0) rel = p.substr(root_.size() + 1);
    std::string s = toUtf8(slashes(rel));
    if (r.archive >= 0) s += ":" + archives_[static_cast<size_t>(r.archive)]->entries()[r.index].name;
    return s;
}

Status GameFiles::read(const std::string& name, std::vector<uint8_t>& out, std::string* origin) const {
    out.clear();
    if (origin) origin->clear();  // bug 0091 item 5: never leave the previous call's origin on NotFound
    const Ref* r = lookup(name);
    if (!r) return Status::fail(Err::NotFound, "gamefiles: no file named '" + name + "'");
    if (origin) *origin = originOf(*r);
    if (r->archive >= 0) return archives_[static_cast<size_t>(r->archive)]->read(r->index, out);
    return readWholeFile(loosePaths_[r->index], out);
}

bool GameFiles::exists(const std::string& name) const { return lookup(name) != nullptr; }
size_t GameFiles::fileCount() const { return final_.size(); }

std::vector<std::string> GameFiles::names() const {
    std::vector<std::string> v;
    v.reserve(final_.size());
    for (const auto& kv : final_) v.push_back(kv.first);
    std::sort(v.begin(), v.end());
    return v;
}

std::string GameFiles::origin(const std::string& name) const {
    const Ref* r = lookup(name);
    return r ? originOf(*r) : std::string();
}

uint64_t GameFiles::size(const std::string& name) const {
    const Ref* r = lookup(name);
    return r ? r->size : 0;
}

std::vector<GameFiles::Candidate> GameFiles::candidates(const std::string& name) const {
    std::vector<Candidate> v;
    std::string key = lowerAscii(name);
    const Ref* act = lookup(key);
    for (const Layer& L : layers_) {
        auto it = L.map.find(key);
        if (it == L.map.end()) continue;
        auto add = [&](const Ref& r) {
            Candidate c;
            c.origin = originOf(r);
            c.layer = L.label;
            c.fromPak = r.archive >= 0;
            c.size = r.size;
            c.active = act && act->archive == r.archive && act->index == r.index;
            v.push_back(c);
        };
        add(it->second);
        auto sh = L.shadowed.find(key);
        if (sh != L.shadowed.end()) {
            for (const Ref& r : sh->second) add(r);
        }
    }
    return v;
}

}} // namespace c5r::gamefs
