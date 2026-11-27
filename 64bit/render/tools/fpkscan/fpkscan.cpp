// render/tools/fpkscan/fpkscan.cpp - pack lister, extractor and whole-install sweep (task 2901). Own code.
//
//   fpkscan list PACK.fpk
//   fpkscan extract PACK.fpk OUTDIR [NAME...]          OUTDIR must be under C:/c5-64bit-work or F:/c5-64bit-work
//        (never a game folder; D: is storage only since D133, ticket 0181; large extracts belong on F:)
//   fpkscan sweep INSTALL_ROOT [--out DIR] [--mods DIR] [--warp]
//        opens every .fpk under Resource/ and Assets/, parses every .dds in them and every loose .dds under Assets/
//        (and under --mods), creates each as a D3D11 texture on a hardware device (WARP only if there is none, or with
//        --warp) and writes sweep_files.csv, sweep_archives.csv and sweep_summary.txt to DIR
//        (default C:/c5-64bit-work/2900/2901/sweep). Exit 0 when every file parsed and created, 4 when any failed
//        (never 3: that is the launch gates' "lock held"), 1 on an I/O error, 2 on a usage error.
//   fpkscan dups INSTALL_ROOT [--out DIR] [--mods DIR] [--dx9low]
//        rebuilds the game's lookup (GameFiles, keepShadowed) and writes every name registered more than once, with
//        a content hash per candidate: dups.csv, dups_summary.txt.
//   fpkscan lookup INSTALL_ROOT [--mods DIR] NAME...   prints where each name resolves and every shadowed candidate.
// No window is created at any point.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/fpk.h"
#include "c5r/gamefs/gamefiles.h"

using namespace c5r;
using namespace c5r::gamefs;

namespace {

std::string u8(const std::wstring& w) { return toUtf8(w); }
std::wstring w16(const std::string& s) { return fromUtf8(s); }

std::wstring fwd(std::wstring s) {
    for (wchar_t& c : s) {
        if (c == L'\\') c = L'/';
    }
    return s;
}
std::wstring lowerW(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return s;
}
bool endsWithI(const std::wstring& s, const wchar_t* suf) {
    std::wstring a = lowerW(s), b = lowerW(suf);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}
// The work roots (ticket 0181, D133: D: is storage only; before, only d:/c5-64bit-work/ was accepted).
bool underWork(const std::wstring& p) {
    std::wstring l = lowerW(fwd(p));
    return l.compare(0, 17, L"c:/c5-64bit-work/") == 0 || l.compare(0, 17, L"f:/c5-64bit-work/") == 0;
}
void mkdirs(const std::wstring& p) {
    std::wstring cur;
    for (size_t i = 0; i < p.size(); ++i) {
        cur += p[i];
        if ((p[i] == L'/' || p[i] == L'\\') && cur.size() > 3) CreateDirectoryW(cur.c_str(), nullptr);
    }
    CreateDirectoryW(p.c_str(), nullptr);
}
void walk(const std::wstring& dir, const wchar_t* suffix, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subs;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) subs.push_back(dir + L"\\" + n);
        else if (endsWithI(n, suffix)) out.push_back(dir + L"\\" + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(subs.begin(), subs.end());
    for (const auto& s : subs) walk(s, suffix, out);
}
double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}
std::string csv(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}
uint64_t fnv(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
std::string hex64(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}
Status readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Status::fail(Err::Io, "cannot open " + u8(path));
    LARGE_INTEGER li;
    GetFileSizeEx(f, &li);
    out.resize(static_cast<size_t>(li.QuadPart));
    DWORD got = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(f);
    if (!ok || got != out.size()) return Status::fail(Err::Io, "cannot read " + u8(path));
    return Status::Ok();
}

// Terrain-side art: the folders and name parts the brief lists.
bool terrainSide(const std::string& where, const std::string& name) {
    std::string w = lowerAscii(where), n = lowerAscii(name);
    if (w.find("assets/terrain/") != std::string::npos) return true;
    if (w.find("terraintextures") != std::string::npos || w.find("decaltextures") != std::string::npos) return true;
    if (w.find("/dlc/vt/terrain/") != std::string::npos || w.find("/dlc/vt/water/") != std::string::npos) return true;
    static const char* parts[] = {"terrain", "water", "river", "decal", "border", "fog", "coast", "ice", "ocean", "lake"};
    for (const char* p : parts) {
        if (n.find(p) != std::string::npos) return true;
    }
    return false;
}

struct Device {
    ID3D11Device* dev = nullptr;
    std::string desc;
    bool warp = false;
    Status create(bool forceWarp) {
        D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT h = E_FAIL;
        if (!forceWarp)
            h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 2, D3D11_SDK_VERSION, &dev, nullptr, nullptr);
        if (FAILED(h)) {
            warp = true;
            h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, 2, D3D11_SDK_VERSION, &dev, nullptr, nullptr);
        }
        if (FAILED(h)) return Status::fail(Err::Device, "D3D11CreateDevice failed");
        IDXGIDevice* dd = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dd)))) {
            IDXGIAdapter* ad = nullptr;
            if (SUCCEEDED(dd->GetAdapter(&ad))) {
                DXGI_ADAPTER_DESC d;
                if (SUCCEEDED(ad->GetDesc(&d))) desc = u8(d.Description);
                ad->Release();
            }
            dd->Release();
        }
        char fls[32];
        std::snprintf(fls, sizeof fls, " FL %x", static_cast<unsigned>(dev->GetFeatureLevel()));
        desc += std::string(warp ? " (WARP)" : " (hardware)") + fls;
        return Status::Ok();
    }
    ~Device() { if (dev) dev->Release(); }
};

struct Row {
    std::string where, name, srcFmt, dxgi, parse, create;
    uint64_t bytes = 0;
    uint32_t w = 0, h = 0, d = 0, mips = 0, arr = 0;
    bool cube = false, vol = false, conv = false, terrain = false;
};

void testDds(Device& dv, const uint8_t* p, size_t n, Row& r) {
    DdsImage img;
    Status st = parseDds(p, n, img);
    r.bytes = n;
    if (!st.ok()) {
        r.parse = st.str();
        return;
    }
    r.parse = "ok";
    r.srcFmt = img.sourceFormat;
    r.dxgi = dxgiFormatName(img.format);
    r.w = img.width;
    r.h = img.height;
    r.d = img.depth;
    r.mips = img.mipLevels;
    r.arr = img.arraySize;
    r.cube = img.cube;
    r.vol = img.dim == DdsDim::Tex3D;
    r.conv = img.converted;
    ID3D11Resource* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    st = createTexture(dv.dev, img, &tex, &srv);
    r.create = st.ok() ? "ok" : st.str();
    if (srv) srv->Release();
    if (tex) tex->Release();
}

int cmdList(const std::wstring& pack) {
    FpkArchive a;
    Status st = a.open(pack);
    if (!st.ok()) {
        std::fprintf(stderr, "%s\n", st.str().c_str());
        return 1;
    }
    for (const FpkEntry& e : a.entries()) std::printf("%s\t%u\t%u\t%u\n", e.name.c_str(), e.size, e.offset, e.a);
    std::fprintf(stderr, "%zu entries, %llu bytes\n", a.entries().size(), static_cast<unsigned long long>(a.fileSize()));
    return 0;
}

int cmdExtract(const std::wstring& pack, const std::wstring& out, const std::vector<std::string>& names) {
    if (!underWork(out)) {
        std::fprintf(stderr, "extract: OUTDIR must be under C:/c5-64bit-work or F:/c5-64bit-work\n");
        return 2;
    }
    FpkArchive a;
    Status st = a.open(pack);
    if (!st.ok()) {
        std::fprintf(stderr, "%s\n", st.str().c_str());
        return 1;
    }
    mkdirs(out);
    std::set<std::string> want;
    for (const auto& n : names) want.insert(lowerAscii(n));
    int n = 0;
    for (size_t i = 0; i < a.entries().size(); ++i) {
        const FpkEntry& e = a.entries()[i];
        if (!want.empty() && !want.count(lowerAscii(e.name))) continue;
        std::vector<uint8_t> b;
        a.read(i, b);
        std::wstring p = out + L"/" + w16(e.name);
        HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "cannot write %s\n", u8(p).c_str());
            return 1;
        }
        DWORD wr = 0;
        WriteFile(f, b.data(), static_cast<DWORD>(b.size()), &wr, nullptr);
        CloseHandle(f);
        ++n;
    }
    std::printf("extracted %d files to %s\n", n, u8(out).c_str());
    return 0;
}

int cmdSweep(const std::wstring& root, std::wstring out, const std::wstring& mods, bool forceWarp) {
    if (out.empty()) out = L"C:/c5-64bit-work/2900/2901/sweep";
    if (!underWork(out)) {
        std::fprintf(stderr, "sweep: --out must be under C:/c5-64bit-work or F:/c5-64bit-work\n");
        return 2;
    }
    mkdirs(out);
    Device dv;
    Status st = dv.create(forceWarp);
    if (!st.ok()) {
        std::fprintf(stderr, "%s\n", st.str().c_str());
        return 1;
    }
    std::printf("device: %s\n", dv.desc.c_str());
    double t0 = now();
    std::vector<std::wstring> packs;
    walk(root + L"\\Resource", L".fpk", packs);
    walk(root + L"\\Assets", L".fpk", packs);
    std::vector<Row> rows;
    FILE* fa = _wfopen((out + L"/sweep_archives.csv").c_str(), L"wb");
    if (!fa) return 1;
    std::fprintf(fa, "archive,status,bytes,entries,dds,dds_parsed,dds_created\n");
    int opened = 0;
    uint64_t entries = 0, packBytes = 0;
    std::map<std::string, int> extCount;
    std::vector<std::string> archFail;
    for (const std::wstring& p : packs) {
        std::string rel = u8(fwd(p.substr(root.size() + 1)));
        FpkArchive a;
        st = a.open(p);
        if (!st.ok()) {
            archFail.push_back(rel + ": " + st.str());
            std::fprintf(fa, "%s,%s,0,0,0,0,0\n", csv(rel).c_str(), csv(st.str()).c_str());
            continue;
        }
        ++opened;
        entries += a.entries().size();
        packBytes += a.fileSize();
        int dds = 0, parsed = 0, created = 0;
        for (size_t i = 0; i < a.entries().size(); ++i) {
            const FpkEntry& e = a.entries()[i];
            std::string ln = lowerAscii(e.name);
            size_t dot = ln.rfind('.');
            extCount[dot == std::string::npos ? "(none)" : ln.substr(dot)]++;
            if (ln.size() < 4 || ln.compare(ln.size() - 4, 4, ".dds") != 0) continue;
            ++dds;
            size_t n = 0;
            const uint8_t* v = a.view(i, &n);
            Row r;
            r.where = rel;
            r.name = e.name;
            r.terrain = terrainSide(rel, e.name);
            testDds(dv, v, n, r);
            parsed += r.parse == "ok";
            created += r.create == "ok";
            rows.push_back(r);
        }
        std::fprintf(fa, "%s,ok,%llu,%zu,%d,%d,%d\n", csv(rel).c_str(), static_cast<unsigned long long>(a.fileSize()),
                     a.entries().size(), dds, parsed, created);
    }
    fclose(fa);
    double tPacks = now() - t0;
    std::vector<std::pair<std::wstring, std::wstring>> looseRoots{{root + L"\\Assets", root}};
    if (!mods.empty()) looseRoots.emplace_back(mods, mods);
    size_t packRows = rows.size();
    for (const auto& lr : looseRoots) {
        std::vector<std::wstring> files;
        walk(lr.first, L".dds", files);
        for (const std::wstring& f : files) {
            std::vector<uint8_t> b;
            Row r;
            r.where = u8(fwd(lr.second == root ? f.substr(root.size() + 1) : f));
            size_t sl = f.find_last_of(L"\\/");
            r.name = u8(f.substr(sl + 1));
            r.terrain = terrainSide(r.where, r.name);
            st = readFile(f, b);
            if (!st.ok()) {
                r.parse = st.str();
            } else {
                testDds(dv, b.data(), b.size(), r);
            }
            rows.push_back(r);
        }
    }
    double tAll = now() - t0;
    FILE* ff = _wfopen((out + L"/sweep_files.csv").c_str(), L"wb");
    if (!ff) return 1;
    std::fprintf(ff, "source,where,name,bytes,source_format,dxgi,width,height,depth,mips,array,cube,volume,converted,terrain_side,parse,create\n");
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::fprintf(ff, "%s,%s,%s,%llu,%s,%s,%u,%u,%u,%u,%u,%d,%d,%d,%d,%s,%s\n", i < packRows ? "fpk" : "loose",
                     csv(r.where).c_str(), csv(r.name).c_str(), static_cast<unsigned long long>(r.bytes), csv(r.srcFmt).c_str(),
                     r.dxgi.c_str(), r.w, r.h, r.d, r.mips, r.arr, r.cube, r.vol, r.conv, r.terrain, csv(r.parse).c_str(),
                     csv(r.create).c_str());
    }
    fclose(ff);
    // Summary.
    struct Cnt { int files = 0, parsed = 0, created = 0; };
    std::map<std::string, Cnt> byFmt, byFmtTerrain, byWhereTerrain;
    std::map<std::string, int> fails;
    Cnt tot[2], terr[2];
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        int k = i < packRows ? 0 : 1;
        bool p = r.parse == "ok", c = r.create == "ok";
        std::string key = p ? (r.srcFmt + " -> " + r.dxgi + (r.cube ? " cube" : "") + (r.vol ? " volume" : "") +
                               (r.conv ? " (converted)" : ""))
                            : "(parse failed)";
        auto bump = [&](Cnt& x) { x.files++; x.parsed += p; x.created += c; };
        bump(tot[k]);
        bump(byFmt[std::string(k ? "loose " : "fpk ") + key]);
        if (r.terrain) {
            bump(terr[k]);
            bump(byFmtTerrain[std::string(k ? "loose " : "fpk ") + key]);
            std::string w = r.where;
            if (k) {
                size_t sl = w.rfind('/');
                w = sl == std::string::npos ? w : w.substr(0, sl);
            }
            bump(byWhereTerrain[w]);
        }
        if (!p || !c) fails[(p ? r.create : r.parse).substr(0, 90)]++;
    }
    FILE* fs = _wfopen((out + L"/sweep_summary.txt").c_str(), L"wb");
    if (!fs) return 1;
    auto both = [&](FILE* f) {
        std::fprintf(f, "fpkscan sweep of %s\n", u8(fwd(root)).c_str());
        if (!mods.empty()) std::fprintf(f, "mods root: %s\n", u8(fwd(mods)).c_str());
        std::fprintf(f, "device: %s\n", dv.desc.c_str());
        std::fprintf(f, "archives: %d opened of %zu found (%llu bytes, %llu entries)\n", opened, packs.size(),
                     static_cast<unsigned long long>(packBytes), static_cast<unsigned long long>(entries));
        for (const auto& s : archFail) std::fprintf(f, "  archive FAILED: %s\n", s.c_str());
        std::fprintf(f, "entries by extension:");
        for (const auto& kv : extCount) std::fprintf(f, " %s %d;", kv.first.c_str(), kv.second);
        std::fprintf(f, "\n");
        std::fprintf(f, "DDS in packs: %d files, %d parsed, %d created\n", tot[0].files, tot[0].parsed, tot[0].created);
        std::fprintf(f, "DDS loose:    %d files, %d parsed, %d created\n", tot[1].files, tot[1].parsed, tot[1].created);
        std::fprintf(f, "terrain-side DDS: packs %d/%d/%d, loose %d/%d/%d (files/parsed/created)\n", terr[0].files,
                     terr[0].parsed, terr[0].created, terr[1].files, terr[1].parsed, terr[1].created);
        std::fprintf(f, "time: packs %.1f s, total %.1f s\n", tPacks, tAll);
        std::fprintf(f, "\nby format (files / parsed / created):\n");
        for (const auto& kv : byFmt)
            std::fprintf(f, "  %6d %6d %6d  %s\n", kv.second.files, kv.second.parsed, kv.second.created, kv.first.c_str());
        std::fprintf(f, "\nterrain-side by format:\n");
        for (const auto& kv : byFmtTerrain)
            std::fprintf(f, "  %6d %6d %6d  %s\n", kv.second.files, kv.second.parsed, kv.second.created, kv.first.c_str());
        std::fprintf(f, "\nterrain-side by archive / folder:\n");
        for (const auto& kv : byWhereTerrain)
            std::fprintf(f, "  %6d %6d %6d  %s\n", kv.second.files, kv.second.parsed, kv.second.created, kv.first.c_str());
        std::fprintf(f, "\nfailures by reason:\n");
        for (const auto& kv : fails) std::fprintf(f, "  %6d  %s\n", kv.second, kv.first.c_str());
    };
    both(fs);
    fclose(fs);
    both(stdout);
    // 4, not 3: exit code 3 is the launch gates' "lock held" (perf/quietlock/gate.py; bug 0091 item 6).
    return (tot[0].files == tot[0].created && tot[1].files == tot[1].created && archFail.empty()) ? 0 : 4;
}

int cmdDups(const std::wstring& root, std::wstring out, const std::wstring& mods, bool dx9low) {
    if (out.empty()) out = L"C:/c5-64bit-work/2900/2901/dups";
    if (!underWork(out)) {
        std::fprintf(stderr, "dups: --out must be under C:/c5-64bit-work or F:/c5-64bit-work\n");
        return 2;
    }
    mkdirs(out);
    GameFiles gf;
    GameFiles::Options o;
    o.keepShadowed = true;
    o.includeDx9Low = dx9low;
    if (!mods.empty()) o.extraLooseRoots.push_back(mods);
    Status st = gf.open(root, o);
    if (!st.ok()) {
        std::fprintf(stderr, "%s\n", st.str().c_str());
        return 1;
    }
    FILE* fd = _wfopen((out + L"/dups.csv").c_str(), L"wb");
    if (!fd) return 1;
    std::fprintf(fd, "name,candidates,distinct_contents,terrain_side,active,layer,origin,bytes,fnv64\n");
    int multi = 0, differ = 0, terrMulti = 0, terrDiffer = 0;
    std::map<std::string, std::pair<int, int>> byExt;   // ext -> (multi, differ)
    std::map<std::string, std::pair<int, int>> byPair;  // "winner layer/kind <- loser layer/kind" -> (count, differ)
    std::vector<uint8_t> buf;
    std::map<std::wstring, std::unique_ptr<FpkArchive>> cache;
    for (const std::string& n : gf.names()) {
        std::vector<GameFiles::Candidate> c = gf.candidates(n);
        if (c.size() < 2) continue;
        ++multi;
        std::vector<std::string> hashes;
        std::set<std::string> distinct;
        for (const auto& k : c) {
            // Read the candidate's bytes through its origin: packs by name, loose by path.
            std::string h = "?";
            size_t colon = k.origin.find(".fpk:");
            if (k.fromPak && colon != std::string::npos) {
                std::string packRel = k.origin.substr(0, colon + 4);
                std::wstring pp = packRel.size() > 1 && packRel[1] == ':' ? w16(packRel) : root + L"/" + w16(packRel);
                std::unique_ptr<FpkArchive>& a = cache[pp];
                if (!a) {
                    a.reset(new FpkArchive);
                    a->open(pp);
                }
                if (a->isOpen()) {
                    int idx = a->find(k.origin.substr(colon + 5));
                    size_t sz = 0;
                    const uint8_t* v = idx >= 0 ? a->view(static_cast<size_t>(idx), &sz) : nullptr;
                    if (v) h = hex64(fnv(v, sz));
                }
            } else {
                std::wstring lp = k.origin.size() > 1 && k.origin[1] == ':' ? w16(k.origin) : root + L"/" + w16(k.origin);
                if (readFile(lp, buf).ok()) h = hex64(fnv(buf.data(), buf.size()));
            }
            hashes.push_back(h);
            distinct.insert(h);
        }
        bool dif = distinct.size() > 1;
        differ += dif;
        bool terr = terrainSide(c[0].origin, n);
        terrMulti += terr;
        terrDiffer += terr && dif;
        size_t dot = n.rfind('.');
        auto& e = byExt[dot == std::string::npos ? "(none)" : n.substr(dot)];
        e.first++;
        e.second += dif;
        const GameFiles::Candidate* win = nullptr;
        for (const auto& k : c) {
            if (k.active) win = &k;
        }
        for (const auto& k : c) {
            if (&k == win || !win) continue;
            std::string key = win->layer + (win->fromPak ? "/pack" : "/loose") + " over " + k.layer + (k.fromPak ? "/pack" : "/loose");
            auto& pr = byPair[key];
            pr.first++;
            pr.second += dif;
        }
        for (size_t i = 0; i < c.size(); ++i) {
            std::fprintf(fd, "%s,%zu,%zu,%d,%d,%s,%s,%llu,%s\n", csv(n).c_str(), c.size(), distinct.size(), terr, c[i].active,
                         c[i].layer.c_str(), csv(c[i].origin).c_str(), static_cast<unsigned long long>(c[i].size),
                         hashes[i].c_str());
        }
    }
    fclose(fd);
    FILE* fs = _wfopen((out + L"/dups_summary.txt").c_str(), L"wb");
    if (!fs) return 1;
    auto both = [&](FILE* f) {
        std::fprintf(f, "fpkscan dups of %s (dx9low %d)\n", u8(fwd(root)).c_str(), dx9low);
        if (!mods.empty()) std::fprintf(f, "extra loose root: %s\n", u8(fwd(mods)).c_str());
        std::fprintf(f, "packages: %zu, mounted packs: %zu, resolvable names: %zu\n", gf.packages().size(),
                     gf.mountedPacks().size(), gf.fileCount());
        for (const auto& w : gf.warnings()) std::fprintf(f, "  warning: %s\n", w.c_str());
        std::fprintf(f, "names registered more than once: %d, with differing contents: %d\n", multi, differ);
        std::fprintf(f, "terrain-side: %d more than once, %d differing\n", terrMulti, terrDiffer);
        std::fprintf(f, "\nby extension (multi / differing):\n");
        for (const auto& kv : byExt) std::fprintf(f, "  %6d %6d  %s\n", kv.second.first, kv.second.second, kv.first.c_str());
        std::fprintf(f, "\nwinner over shadowed candidate (pairs / of them differing):\n");
        for (const auto& kv : byPair) std::fprintf(f, "  %6d %6d  %s\n", kv.second.first, kv.second.second, kv.first.c_str());
        std::fprintf(f, "\nmounted packs in mount order:\n");
        for (const auto& p : gf.mountedPacks()) std::fprintf(f, "  %s\n", u8(fwd(p.substr(root.size() + 1))).c_str());
        std::fprintf(f, "\npackages in registration order (priority, file):\n");
        for (const auto& p : gf.packages()) std::fprintf(f, "  %4d  %s\n", p.priority, u8(fwd(p.file.substr(root.size() + 1))).c_str());
    };
    both(fs);
    fclose(fs);
    std::printf("names: %zu, multi %d, differing %d; written to %s\n", gf.fileCount(), multi, differ, u8(out).c_str());
    return 0;
}

int cmdLookup(const std::wstring& root, const std::wstring& mods, const std::vector<std::string>& names) {
    GameFiles gf;
    GameFiles::Options o;
    o.keepShadowed = true;
    if (!mods.empty()) o.extraLooseRoots.push_back(mods);
    Status st = gf.open(root, o);
    if (!st.ok()) {
        std::fprintf(stderr, "%s\n", st.str().c_str());
        return 1;
    }
    for (const auto& n : names) {
        std::printf("%s -> %s\n", n.c_str(), gf.exists(n) ? gf.origin(n).c_str() : "(not found)");
        for (const auto& c : gf.candidates(n))
            std::printf("    %s %-8s %s %llu\n", c.active ? "*" : " ", c.layer.c_str(), c.origin.c_str(),
                        static_cast<unsigned long long>(c.size));
    }
    return 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage: fpkscan list PACK | extract PACK OUTDIR [NAME...] | sweep ROOT [--out DIR] [--mods DIR] [--warp]\n"
                 "       | dups ROOT [--out DIR] [--mods DIR] [--dx9low] | lookup ROOT [--mods DIR] NAME...\n");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    std::wstring cmd = argv[1];
    std::vector<std::wstring> pos;
    std::wstring out, mods;
    bool warp = false, dx9low = false;
    for (int i = 2; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--out" && i + 1 < argc) out = argv[++i];
        else if (a == L"--mods" && i + 1 < argc) mods = argv[++i];
        else if (a == L"--warp") warp = true;
        else if (a == L"--dx9low") dx9low = true;
        else pos.push_back(a);
    }
    if (pos.empty()) {
        usage();
        return 2;
    }
    std::vector<std::string> rest;
    for (size_t i = 1; i < pos.size(); ++i) rest.push_back(u8(pos[i]));
    std::wstring root = pos[0];
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    if (cmd == L"list") return cmdList(pos[0]);
    if (cmd == L"extract") {
        if (pos.size() < 2) {
            usage();
            return 2;
        }
        std::vector<std::string> names(rest.begin() + 1, rest.end());
        return cmdExtract(pos[0], pos[1], names);
    }
    if (cmd == L"sweep") return cmdSweep(root, out, mods, warp);
    if (cmd == L"dups") return cmdDups(root, out, mods, dx9low);
    if (cmd == L"lookup") return cmdLookup(root, mods, rest);
    usage();
    return 2;
}
