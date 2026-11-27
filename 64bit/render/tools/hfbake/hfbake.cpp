// render/tools/hfbake/hfbake.cpp - the server side of the height-field tap (task 2904). Own code.
//
// The decision of task 2904 is to tap the EXE's height field, not to re-bake it (asset-pipeline.md section 4), so this
// tool does not bake: it turns what the tap or a debugger dump delivers into the dense grid, writes it as a raw file
// and a 16-bit PNG preview, and compares two grids (a second tap, a dump of epic 3000's ground truth, or a future
// re-bake) with a stated tolerance.
//
//   hfbake decode --obj F --dir16 F --pool16 F [--dir8 F --pool8 F] [--wrap] [--water Z] [--seed N] [--h2w F] OUT
//        raw copies of the EXE's memory: F(obj) = 0xC8+ bytes at 0xD91270; dir16 = [0xD9128C], tilesX*tilesY*8 bytes;
//        pool16 = [0xD91298], [0xD9129C]*512 bytes; dir8 = [0xD912CC]; pool8 = [0xD912D8], [0xD912DC]*256 bytes;
//        --h2w: 64 bytes at 0xD91030. Writes OUT.c5hf, OUT.r16 (u16 LE, row-major), OUT.png (stretched), OUT.txt.
//   hfbake show TAP.c5hf OUT                     a C5HF file (the shell's tap or a converted dump) to .r16/.png/.txt
//   hfbake compare A.c5hf B.c5hf [--tol N]       statistics; exit 0 when no sample differs by more than N (default 0),
//                                                3 when some do, 1 on an error
//   hfbake synth W H OUT                         a synthetic W x H plot field run through the whole memory -> tap path
// OUT must lie under C:/c5-64bit-work or F:/c5-64bit-work (nothing is ever written into a game folder; D: is storage
// only since D133, ticket 0181). No window is created.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/heightfield/heightfield.h"

using namespace c5r;
using namespace c5r::heightfield;

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}
bool underWork(const std::string& p) {   // ticket 0181 (D133): C:/F: work roots; before, only d:/c5-64bit-work/
    const std::string l = lower(p);
    return l.compare(0, 17, "c:/c5-64bit-work/") == 0 || l.compare(0, 17, "f:/c5-64bit-work/") == 0;
}
std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void mkdirsFor(const std::string& prefix) {
    std::string p = prefix;
    for (char& c : p) {
        if (c == '\\') c = '/';
    }
    for (size_t i = 3; i < p.size(); ++i) {
        if (p[i] == '/') CreateDirectoryA(p.substr(0, i).c_str(), nullptr);
    }
}

Status readFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return Status::fail(Err::Io, "cannot open " + path);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) {
        std::fclose(f);
        return Status::fail(Err::Io, "cannot size " + path);
    }
    out.resize(static_cast<size_t>(n));
    const size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    if (got != out.size()) return Status::fail(Err::Io, "short read " + path);
    return Status::Ok();
}

Status writeFile(const std::string& path, const void* data, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return Status::fail(Err::Io, "cannot create " + path);
    const size_t put = n ? std::fwrite(data, 1, n, f) : 0;
    std::fclose(f);
    if (put != n) return Status::fail(Err::Io, "short write " + path);
    return Status::Ok();
}

std::string describe(const HeightGrid& g) {
    uint16_t lo = 0xFFFF, hi = 0;
    for (uint16_t v : g.h) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    char b[512];
    std::snprintf(b, sizeof b,
                  "map %ux%u plots, samples %ux%u, wrapX %d, sx %.6g sy %.6g sz %.6g ox %.6g oy %.6g, waterZ %.6g, "
                  "seed %u\nu16 min %u max %u -> z %.6g .. %.6g world units; byte layer %s\n",
                  g.mapW, g.mapH, g.samplesX, g.samplesY, g.wrapX ? 1 : 0, g.sx, g.sy, g.sz, g.ox, g.oy, g.waterZ,
                  g.mapSeed, lo, hi, lo / 255.0 * g.sz, hi / 255.0 * g.sz, g.mat.empty() ? "absent" : "present");
    return b;
}

Status writeOutputs(const HeightGrid& g, const std::string& out, bool writeTapFile) {
    if (!underWork(out)) return Status::fail(Err::Io, "output prefix must be under C:/c5-64bit-work or F:/c5-64bit-work: " + out);
    mkdirsFor(out);
    if (writeTapFile) {
        std::vector<uint8_t> tap;
        C5R_TRY(writeTap(g, tap));
        C5R_TRY(writeFile(out + ".c5hf", tap.data(), tap.size()));
    }
    C5R_TRY(writeFile(out + ".r16", g.h.data(), g.h.size() * 2));
    C5R_TRY(writePng16(wide(out + ".png"), g.samplesX, g.samplesY, g.h, true));
    const std::string d = describe(g);
    C5R_TRY(writeFile(out + ".txt", d.data(), d.size()));
    std::fputs(d.c_str(), stdout);
    return Status::Ok();
}

const char* arg(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return nullptr;
}
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return true;
    }
    return false;
}

int fail(const Status& s) {
    std::fprintf(stderr, "hfbake: %s\n", s.str().c_str());
    return 1;
}

int cmdDecode(int argc, char** argv) {
    const char* obj = arg(argc, argv, "--obj");
    const char* d16 = arg(argc, argv, "--dir16");
    const char* p16 = arg(argc, argv, "--pool16");
    const char* d8 = arg(argc, argv, "--dir8");
    const char* p8 = arg(argc, argv, "--pool8");
    if (!obj || !d16 || !p16 || (d8 == nullptr) != (p8 == nullptr)) {
        std::fprintf(stderr, "hfbake decode: --obj, --dir16, --pool16 required; --dir8 and --pool8 together\n");
        return 1;
    }
    std::vector<uint8_t> o, a, b, c, e;
    Status s = readFile(obj, o);
    if (s.ok()) s = readFile(d16, a);
    if (s.ok()) s = readFile(p16, b);
    if (s.ok() && d8) s = readFile(d8, c);
    if (s.ok() && p8) s = readFile(p8, e);
    if (!s.ok()) return fail(s);
    HeightGrid g;
    s = gridFromMemory(o.data(), o.size(), a.data(), a.size(), b.data(), b.size(), d8 ? c.data() : nullptr, c.size(),
                       e.data(), e.size(), flag(argc, argv, "--wrap"), g);
    if (!s.ok()) return fail(s);
    if (const char* w = arg(argc, argv, "--water")) g.waterZ = static_cast<float>(std::atof(w));
    if (const char* sd = arg(argc, argv, "--seed")) g.mapSeed = static_cast<uint32_t>(std::strtoul(sd, nullptr, 0));
    if (const char* h = arg(argc, argv, "--h2w")) {
        std::vector<uint8_t> m;
        s = readFile(h, m);
        if (!s.ok()) return fail(s);
        if (m.size() < 64) return fail(Status::fail(Err::Truncated, "--h2w needs 64 bytes"));
        std::memcpy(g.hexToWorld, m.data(), 64);
    }
    s = writeOutputs(g, argv[argc - 1], true);
    return s.ok() ? 0 : fail(s);
}

int cmdShow(int argc, char** argv) {
    if (argc < 4) return fail(Status::fail(Err::NotFound, "usage: hfbake show TAP.c5hf OUT"));
    std::vector<uint8_t> f;
    Status s = readFile(argv[2], f);
    HeightGrid g;
    if (s.ok()) s = readTap(f.data(), f.size(), g);
    if (s.ok()) s = writeOutputs(g, argv[3], false);
    return s.ok() ? 0 : fail(s);
}

int cmdCompare(int argc, char** argv) {
    if (argc < 4) return fail(Status::fail(Err::NotFound, "usage: hfbake compare A.c5hf B.c5hf [--tol N]"));
    std::vector<uint8_t> fa, fb;
    HeightGrid a, b;
    Status s = readFile(argv[2], fa);
    if (s.ok()) s = readFile(argv[3], fb);
    if (s.ok()) s = readTap(fa.data(), fa.size(), a);
    if (s.ok()) s = readTap(fb.data(), fb.size(), b);
    const char* t = arg(argc, argv, "--tol");
    const uint32_t tol = t ? static_cast<uint32_t>(std::strtoul(t, nullptr, 0)) : 0;
    CompareStats st;
    if (s.ok()) s = compare(a, b, tol, st);
    if (!s.ok()) return fail(s);
    std::printf("samples %llu, differing %llu, over tolerance %u: %llu\n", static_cast<unsigned long long>(st.samples),
                static_cast<unsigned long long>(st.differing), tol, static_cast<unsigned long long>(st.overTolerance));
    std::printf("max |d| %u at (%u,%u) = %.6g world units; mean |d| %.6g; rms %.6g\n", st.maxAbs, st.maxX, st.maxY,
                st.maxAbs / 255.0 * a.sz, st.meanAbs, st.rms);
    std::printf("|d| histogram: 0:%llu 1:%llu 2-3:%llu 4-15:%llu 16-255:%llu >=256:%llu; byte layer differing %llu\n",
                static_cast<unsigned long long>(st.hist[0]), static_cast<unsigned long long>(st.hist[1]),
                static_cast<unsigned long long>(st.hist[2]), static_cast<unsigned long long>(st.hist[3]),
                static_cast<unsigned long long>(st.hist[4]), static_cast<unsigned long long>(st.hist[5]),
                static_cast<unsigned long long>(st.matDiffering));
    return st.overTolerance ? 3 : 0;
}

// A synthetic landscape: one smooth hill per plot with a height chosen from the plot's position, so that plots, the
// 64-sample period and the tile structure are all visible in the preview.
int cmdSynth(int argc, char** argv) {
    if (argc < 5) return fail(Status::fail(Err::NotFound, "usage: hfbake synth W H OUT"));
    const uint32_t w = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
    const uint32_t h = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0));
    if (w == 0 || h == 0 || w > kMaxPlots || h > kMaxPlots)
        return fail(Status::fail(Err::BadLayout, "synth: map size out of range"));
    HeightGrid g;
    g.mapW = w;
    g.mapH = h;
    g.samplesX = w * kSamplesPerPlot;
    g.samplesY = h * kSamplesPerPlot;
    g.ox = static_cast<float>(w * 32) * g.sx;
    g.oy = static_cast<float>(h * 32) * g.sy;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    g.mat.resize(g.h.size());
    for (uint32_t y = 0; y < g.samplesY; ++y) {
        for (uint32_t x = 0; x < g.samplesX; ++x) {
            const uint32_t px = x / 64, py = y / 64;
            const uint32_t kind = (px * 7 + py * 13) % 4;  // 0 water, 1 flat, 2 hill, 3 mountain
            const double fx = (x % 64) / 64.0 - 0.5, fy = (y % 64) / 64.0 - 0.5;
            const double r = std::sqrt(fx * fx + fy * fy);
            const double base = kind == 0 ? 0.18 : 0.26;
            const double peak = kind == 2 ? 0.08 : kind == 3 ? 0.30 : 0.0;
            const double v = base + peak * std::max(0.0, std::cos(r * 3.14159265 * 1.2));
            g.h[g.index(x, y)] = static_cast<uint16_t>(std::min(65535.0, v * 65535.0));
            g.mat[g.index(x, y)] = static_cast<uint8_t>(kind);
        }
    }
    MemoryImage m;
    Status s = buildMemoryImage(g, true, m);
    HeightGrid back;
    if (s.ok())
        s = gridFromMemory(m.obj.data(), m.obj.size(), m.dir16.data(), m.dir16.size(), m.pool16.data(),
                           m.pool16.size(), m.dir8.data(), m.dir8.size(), m.pool8.data(), m.pool8.size(), false, back);
    if (!s.ok()) return fail(s);
    CompareStats st;
    s = compare(g, back, 0, st);
    if (!s.ok()) return fail(s);
    if (st.differing || st.matDiffering) return fail(Status::fail(Err::BadLayout, "synth: memory round trip differs"));
    std::printf("synthetic memory image: obj %zu, dir16 %zu, pool16 %zu, dir8 %zu, pool8 %zu bytes; round trip exact\n",
                m.obj.size(), m.dir16.size(), m.pool16.size(), m.dir8.size(), m.pool8.size());
    const std::string out = argv[4];
    if (!underWork(out)) return fail(Status::fail(Err::Io, "output prefix must be under C:/c5-64bit-work or F:/c5-64bit-work"));
    mkdirsFor(out);
    s = writeFile(out + ".obj.bin", m.obj.data(), m.obj.size());
    if (s.ok()) s = writeFile(out + ".dir16.bin", m.dir16.data(), m.dir16.size());
    if (s.ok()) s = writeFile(out + ".pool16.bin", m.pool16.data(), m.pool16.size());
    if (s.ok()) s = writeFile(out + ".dir8.bin", m.dir8.data(), m.dir8.size());
    if (s.ok()) s = writeFile(out + ".pool8.bin", m.pool8.data(), m.pool8.size());
    if (s.ok()) s = writeOutputs(back, out, true);
    return s.ok() ? 0 : fail(s);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string c = argv[1];
        if (c == "decode" && argc >= 3) return cmdDecode(argc, argv);
        if (c == "show") return cmdShow(argc, argv);
        if (c == "compare") return cmdCompare(argc, argv);
        if (c == "synth") return cmdSynth(argc, argv);
    }
    std::fprintf(stderr,
                 "usage:\n  hfbake decode --obj F --dir16 F --pool16 F [--dir8 F --pool8 F] [--wrap] [--water Z] "
                 "[--seed N] [--h2w F] OUT\n  hfbake show TAP.c5hf OUT\n  hfbake compare A.c5hf B.c5hf [--tol N]\n"
                 "  hfbake synth W H OUT\n");
    return 1;
}
