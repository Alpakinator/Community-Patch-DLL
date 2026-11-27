// unitscene - the offline oracle run of the renderer's unit module (epic 0041, issue 4355; research
// unit-renderer.md section 13). The Python driver render/tools/unitscene/oracle.py runs it; game-derived inputs are
// read at run time from the work folders, outputs are numbers only (D83).
//
//   unitscene scene FILE.c5units [...] [--write OUT_DIR]
//       Reads each unit-scene file with the module's reader (c5r/units/scene_file.h) and prints one line per file:
//       "scene FILE ok players N units N figures N" or "scene FILE refused CODE line N: MSG". With --write the scene
//       is written again by the module's writer into OUT_DIR (same name) and read back (round trip). Exit 0 all read,
//       1 one refused (every file is still tried), 2 usage.
//   unitscene pose --list LIST.tsv --rows ROWS.f32 --out OUT.tsv [--gsd-union GSDS.txt]
//       The pose layer (c5r/units/pose.h) against captured palette rows. LIST.tsv columns (tab, header line first):
//       key model model_gsd mesh clip clip_gsd t scale yaw_deg Tx Ty Tz offset nbind [index_count]
//       (index_count, optional: picks among meshes of one name and binding count, as chariot models hold)
//       (gsd "-" = none, "UNION" = the union of every database named in GSDS.txt); ROWS.f32 holds, at float offset
//       `offset`, the captured rows 2 .. 1 + 3 nbind of the instance's block (nbind x 12 floats, row-major 3 x 4).
//       OUT.tsv: key status nbind rot_max_deg trans_max elem_max rot_elem_max sample_us message, and a summary on
//       stdout (clip cache entries, bytes, bind time, sampling time per figure: indications, COMMON 23).
//       Exit 0 every line processed (a failed pose is a row with its status), 1 an input file unreadable, 2 usage.
//   unitscene placement --game INSTALL --heightfield HF.c5hf --db DB --catalogue CAT.tsv --out REPORT.tsv
//       4354's catalogue check (c5r/units/placement_check.h, DoD D); prints its summary and the water level.
//   unitscene prepare --game INSTALL [--mods "A;B"] --db DB --exe EXE --heightfield HF.c5hf [--rows PREFIX] FILE...
//       The module's CPU path (UnitRenderer::open without a device, prepare) on unit-scene files: per unit its status,
//       blocks, draws and warnings; with --rows the plan's palette and block table per file.
//   unitscene program --list LIST.tsv --rows ROWS.f32 --out OUT.tsv [--rows-out OURS.f32] [--gsd-union GSDS.txt]
//       (4406 r2, additive) As pose, for a pose program (the controller's output) through poseFigureProgram; the
//       list and program forms are at cmdProgram.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/anim.h"
#include "c5models/art.h"
#include "c5models/gr2.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/placement_check.h"
#include "c5r/units/pose.h"
#include "c5r/units/scene_file.h"
#include "c5r/units/units.h"
#include "c5r/unitanim/controller_pose.h"
#include "c5r/unitanim/pose_program.h"

using namespace c5r::units;

namespace {

int usage(const char* why) {
    std::fprintf(stderr,
                 "unitscene: %s\nusage: unitscene scene FILE.c5units [...] [--write OUT_DIR]\n"
                 "       unitscene pose --list LIST.tsv --rows ROWS.f32 --out OUT.tsv [--gsd-union GSDS.txt]\n",
                 why);
    return 2;
}

std::wstring widen(const std::string& s) {
    std::wstring w;
    for (unsigned char ch : s) w.push_back(static_cast<wchar_t>(ch));   // paths here are ASCII (work folders)
    return w;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// ------------------------------------------------------------------------------------------------ scene
int cmdScene(int argc, char** argv) {
    std::vector<std::string> files;
    std::string writeDir;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--write") {
            if (i + 1 >= argc) return usage("--write needs a folder");
            writeDir = argv[++i];
        } else if (a.size() > 1 && a[0] == '-') {
            return usage(("unknown argument " + a).c_str());
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) return usage("no scene file");
    int rc = 0;
    for (const std::string& f : files) {
        UnitScene sc;
        SceneFileStatus s = readSceneFile(widen(f), sc);
        if (!s.ok()) {
            std::printf("scene %s refused %s\n", f.c_str(), s.str().c_str());
            rc = 1;
            continue;
        }
        size_t figs = 0;
        for (const UnitInput& u : sc.units) figs += u.figures.size();
        std::printf("scene %s ok players %zu units %zu figures %zu\n", f.c_str(), sc.players.size(), sc.units.size(),
                    figs);
        if (!writeDir.empty()) {
            const std::string out = writeDir + "/" + baseName(f);
            SceneFileStatus w = writeSceneFile(widen(out), sc);
            UnitScene back;
            SceneFileStatus r = w.ok() ? readSceneFile(widen(out), back) : w;
            if (!r.ok() || back.units.size() != sc.units.size() || back.players.size() != sc.players.size()) {
                std::printf("scene %s round trip failed %s\n", f.c_str(), r.str().c_str());
                rc = 1;
            } else {
                std::printf("scene %s round trip ok %s\n", f.c_str(), out.c_str());
            }
        }
    }
    return rc;
}

// ------------------------------------------------------------------------------------------------ pose
struct Strings {
    std::map<std::string, std::unique_ptr<c5m::StringDatabase>> dbs;
    std::vector<std::string> unionList;
    std::unique_ptr<c5m::StringDatabase> unionDb;
    std::string error;

    const c5m::StringDatabase* get(const std::string& name, bool& ok) {
        ok = true;
        if (name == "-" || name.empty()) return nullptr;
        if (name == "UNION") {
            if (!unionDb) {
                unionDb.reset(new c5m::StringDatabase());
                for (const std::string& g : unionList) {
                    std::vector<uint8_t> b;
                    c5m::Error e;
                    if (!readFile(g, b) || !unionDb->add_gsd(b.data(), b.size(), &e)) {
                        error = "cannot load " + g + ": " + e.what;
                        ok = false;
                        return nullptr;
                    }
                }
            }
            return unionDb.get();
        }
        auto it = dbs.find(name);
        if (it != dbs.end()) return it->second.get();
        std::unique_ptr<c5m::StringDatabase> d(new c5m::StringDatabase());
        std::vector<uint8_t> b;
        c5m::Error e;
        if (!readFile(name, b) || !d->add_gsd(b.data(), b.size(), &e)) {
            error = "cannot load " + name + ": " + e.what;
            ok = false;
            return nullptr;
        }
        const c5m::StringDatabase* p = d.get();
        dbs[name] = std::move(d);
        return p;
    }
};

bool loadGr2(Strings& st, const std::string& path, const std::string& gsd, c5m::FileInfo& out, std::string& why) {
    bool ok = false;
    const c5m::StringDatabase* db = st.get(gsd, ok);
    if (!ok) {
        why = st.error;
        return false;
    }
    std::vector<uint8_t> b;
    if (!readFile(path, b)) {
        why = path + ": cannot read";
        return false;
    }
    c5m::Error e;
    if (!c5m::read_gr2(b.data(), b.size(), db, out, &e)) {
        why = path + ": " + e.what;
        return false;
    }
    return true;
}

std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string x;
    while (std::getline(ss, x, '\t')) f.push_back(x);
    return f;
}

// Angle (degrees) of A * B^T for two 3 x 3 rotations given as the left 3 columns of Row34s.
double rotErrDeg(const float* a, const c5m::Row34& b) {
    double m[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += static_cast<double>(a[i * 4 + k]) * b[static_cast<size_t>(j * 4 + k)];
            m[i * 3 + j] = s;
        }
    const double vx = (m[7] - m[5]) / 2, vy = (m[2] - m[6]) / 2, vz = (m[3] - m[1]) / 2;
    return std::atan2(std::sqrt(vx * vx + vy * vy + vz * vz), (m[0] + m[4] + m[8] - 1) / 2) * 180.0 / 3.14159265358979;
}

int cmdPose(int argc, char** argv) {
    std::string list, rowsPath, outPath, unionPath;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--list") list = next();
        else if (a == "--rows") rowsPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--gsd-union") unionPath = next();
        else return usage(("unknown argument " + a).c_str());
    }
    if (list.empty() || rowsPath.empty() || outPath.empty()) return usage("--list, --rows and --out are required");
    Strings st;
    if (!unionPath.empty()) {
        std::ifstream u(unionPath);
        if (!u) {
            std::fprintf(stderr, "unitscene: cannot read %s\n", unionPath.c_str());
            return 1;
        }
        std::string l;
        while (std::getline(u, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) st.unionList.push_back(l);
        }
    }
    std::vector<uint8_t> rb;
    if (!readFile(rowsPath, rb) || rb.size() % 4) {
        std::fprintf(stderr, "unitscene: cannot read %s\n", rowsPath.c_str());
        return 1;
    }
    std::vector<float> rows(rb.size() / 4);
    if (!rows.empty()) std::memcpy(rows.data(), rb.data(), rb.size());
    std::ifstream in(list);
    if (!in) {
        std::fprintf(stderr, "unitscene: cannot read %s\n", list.c_str());
        return 1;
    }
    std::FILE* out = std::fopen(outPath.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "unitscene: cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::fprintf(out, "key\tstatus\tnbind\trot_max_deg\ttrans_max\telem_max\trot_elem_max\tsample_us\tmessage\n");

    // Clip files by lower-case basename -> (path, gsd), filled from the list before any pose.
    std::map<std::string, std::pair<std::string, std::string>> clipFiles;
    std::vector<std::vector<std::string>> lines;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> f = splitTab(line);
        if (f.size() != 14 && f.size() != 15) {
            std::fprintf(stderr, "unitscene: %s: a line with %zu fields (14 or 15 wanted)\n", list.c_str(), f.size());
            std::fclose(out);
            return 1;
        }
        clipFiles[c5m::basename_lower(f[4])] = std::make_pair(f[4], f[5]);
        lines.push_back(f);
    }
    ClipCache cache([&](const std::string& name, c5m::FileInfo& fi, bool& missing, std::string& why) {
        auto it = clipFiles.find(name);
        missing = it == clipFiles.end();
        if (missing) {
            why = name + ": not in the list";
            return false;
        }
        return loadGr2(st, it->second.first, it->second.second, fi, why);
    });
    struct ModelEntry {
        bool ok = false;
        std::string why;
        std::unique_ptr<c5m::FileInfo> file;
        std::map<std::string, std::unique_ptr<PoseModel>> pose;   // per clip file: animations = that one clip
    };
    std::map<std::string, ModelEntry> models;
    double sampleUs = 0;
    size_t sampled = 0, okCount = 0;
    for (const std::vector<std::string>& f : lines) {
        const std::string& key = f[0];
        const std::string &modelPath = f[1], &modelGsd = f[2], &meshName = f[3], &clipPath = f[4];
        const float t = std::strtof(f[6].c_str(), nullptr), s = std::strtof(f[7].c_str(), nullptr);
        const float yaw = static_cast<float>(std::strtod(f[8].c_str(), nullptr) * 3.14159265358979 / 180.0);
        const float T[3] = {std::strtof(f[9].c_str(), nullptr), std::strtof(f[10].c_str(), nullptr),
                            std::strtof(f[11].c_str(), nullptr)};
        const size_t off = std::strtoull(f[12].c_str(), nullptr, 10);
        const size_t nb = std::strtoull(f[13].c_str(), nullptr, 10);
        auto row = [&](const char* status, const std::string& msg) {
            std::fprintf(out, "%s\t%s\t%zu\t\t\t\t\t\t%s\n", key.c_str(), status, nb, msg.c_str());
        };
        ModelEntry& me = models[modelPath];
        if (!me.file && me.why.empty()) {
            me.file.reset(new c5m::FileInfo());
            me.ok = loadGr2(st, modelPath, modelGsd, *me.file, me.why);
        }
        if (!me.ok) {
            row("BadModel", me.why);
            continue;
        }
        // A file can hold several meshes of one name (stock models do): the one whose binding count is the list's,
        // else the first of that name.
        int32_t mi = -1;
        const size_t ic = f.size() > 14 ? std::strtoull(f[14].c_str(), nullptr, 10) : 0;
        for (size_t k = 0; k < me.file->meshes.size() && mi < 0 && ic; ++k)
            if (me.file->meshes[k].name == meshName && me.file->meshes[k].bone_bindings.size() == nb &&
                me.file->meshes[k].indices.size() == ic)
                mi = static_cast<int32_t>(k);
        for (size_t k = 0; k < me.file->meshes.size() && mi < 0; ++k)
            if (me.file->meshes[k].name == meshName && me.file->meshes[k].bone_bindings.size() == nb)
                mi = static_cast<int32_t>(k);
        for (size_t k = 0; k < me.file->meshes.size() && mi < 0; ++k)
            if (me.file->meshes[k].name == meshName) mi = static_cast<int32_t>(k);
        if (mi < 0) {
            row("BadInput", "mesh " + meshName + " not in " + modelPath);
            continue;
        }
        std::unique_ptr<PoseModel>& pm = me.pose[clipPath];
        if (!pm) {
            pm.reset(new PoseModel());
            c5m::FxsAnimation a;
            a.file = baseName(clipPath);
            PoseStatus bs = buildPoseModel(*me.file, std::vector<c5m::FxsAnimation>(1, a),
                                           c5m::basename_lower(modelPath) + "|" + c5m::basename_lower(clipPath), *pm);
            if (!bs.ok()) {
                row(poseErrName(bs.code), bs.msg);
                pm.reset();
                continue;
            }
        }
        if (off + nb * 12 > rows.size()) {
            row("BadInput", "rows past the end of the rows file");
            continue;
        }
        std::vector<MeshPalette> mp;
        const double bindBefore = cache.stats().bind_ms;
        const auto t0 = std::chrono::steady_clock::now();
        PoseStatus ps = poseFigure(cache, *pm, baseName(clipPath), t, true, s, yaw, T,
                                   std::vector<uint32_t>(1, static_cast<uint32_t>(mi)), mp);
        // sampling only: the time of a first-use bind (file read and curve decode) is the cache's, not the pose's
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() -
                          (cache.stats().bind_ms - bindBefore) * 1000.0;
        if (!ps.ok()) {
            row(poseErrName(ps.code), ps.msg);
            continue;
        }
        if (mp[0].rows.size() != nb) {
            row("BadInput", "the mesh binds " + std::to_string(mp[0].rows.size()) + " bones, the list says " +
                                std::to_string(nb));
            continue;
        }
        sampleUs += us;
        ++sampled;
        double rmax = 0, tmax = 0, emax = 0, remax = 0;
        for (size_t k = 0; k < nb; ++k) {
            const float* cap = &rows[off + k * 12];
            const c5m::Row34& our = mp[0].rows[k];
            rmax = std::fmax(rmax, rotErrDeg(cap, our));
            double tx = cap[3] - our[3], ty = cap[7] - our[7], tz = cap[11] - our[11];
            tmax = std::fmax(tmax, std::sqrt(tx * tx + ty * ty + tz * tz));
            for (int j = 0; j < 12; ++j) {
                const double d = std::fabs(static_cast<double>(cap[j]) - our[static_cast<size_t>(j)]);
                emax = std::fmax(emax, d);
                if (j % 4 != 3) remax = std::fmax(remax, d);
            }
        }
        ++okCount;
        std::fprintf(out, "%s\tOk\t%zu\t%.6g\t%.6g\t%.6g\t%.6g\t%.3f\t\n", key.c_str(), nb, rmax, tmax, emax, remax, us);
    }
    std::fclose(out);
    const ClipCache::Stats& cs = cache.stats();
    std::printf("pose lines %zu ok %zu; clip cache entries %zu (failed %zu) binds %zu hits %zu bytes %zu bind_ms %.1f; "
                "sampling %.2f us per figure-mesh over %zu (workstation indication, COMMON 23)\n",
                lines.size(), okCount, cs.entries, cs.failed_entries, cs.binds, cs.hits, cs.bytes, cs.bind_ms,
                sampled ? sampleUs / static_cast<double>(sampled) : 0.0, sampled);
    return 0;
}

// ------------------------------------------------------------------------------------------------ placement
// 4354's catalogue check (placement_check.h) as the oracle's D part: prints the summary block and the height field's
// water level, writes the per-figure report.
int cmdPlacement(int argc, char** argv) {
    std::string game, hf, db, cat, out;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--game") game = next();
        else if (a == "--heightfield") hf = next();
        else if (a == "--db") db = next();
        else if (a == "--catalogue") cat = next();
        else if (a == "--out") out = next();
        else return usage(("unknown argument " + a).c_str());
    }
    if (game.empty() || hf.empty() || db.empty() || cat.empty() || out.empty())
        return usage("placement needs --game --heightfield --db --catalogue --out");
    std::vector<uint8_t> hfb, catb;
    if (!readFile(hf, hfb) || !readFile(cat, catb)) {
        std::fprintf(stderr, "unitscene: cannot read %s or %s\n", hf.c_str(), cat.c_str());
        return 1;
    }
    c5m::SqliteTables tables;
    c5m::Error e;
    if (!c5m::SqliteTables::available() || !tables.open(db, &e)) {
        std::fprintf(stderr, "unitscene: database %s: %s\n", db.c_str(), e.what.c_str());
        return 1;
    }
    c5r::gamefs::GameFiles files;
    c5r::Status s = files.open(c5r::gamefs::fromUtf8(game));
    FormationTable table;
    if (s.ok()) s = table.load(files, c5r::gamefs::fromUtf8(game));
    c5r::heightfield::HeightGrid g;
    if (s.ok()) s = c5r::heightfield::readTap(hfb.data(), hfb.size(), g);
    PlacementColumns cols;
    if (s.ok()) s = loadPlacementColumns(tables, cols);
    std::vector<CatalogueUnit> units;
    if (s.ok()) s = parseCatalogue(std::string(catb.begin(), catb.end()), units);
    CheckTolerances tol;
    CatalogueReport r;
    if (s.ok()) s = checkCatalogue(table, g, cols, units, g.waterZ, tol, r);
    if (!s.ok()) {
        std::fprintf(stderr, "unitscene: placement: %s\n", s.str().c_str());
        return 1;
    }
    const std::string rep = catalogueReportText(r, tol);
    std::FILE* o = std::fopen(out.c_str(), "wb");
    if (!o || std::fwrite(rep.data(), 1, rep.size(), o) != rep.size()) {
        if (o) std::fclose(o);
        std::fprintf(stderr, "unitscene: cannot write %s\n", out.c_str());
        return 1;
    }
    std::fclose(o);
    std::printf("placement water_level %.9g formations %zu units %zu multi %d within %d heights %d within %d failed %d\n",
                static_cast<double>(g.waterZ), table.size(), units.size(), r.multi, r.multiWithin, r.heightFigures,
                r.heightWithin, r.failed);
    return 0;
}

// ------------------------------------------------------------------------------------------------ prepare
// The module's whole CPU path (UnitRenderer::open without a device, prepare: L0 art, L1 models, L2 pose, L3 placement)
// on unit-scene files: one line per unit "unit FILE PLAYER ID CODE blocks N draws N warnings W | TEXT". With --rows
// OUT_PREFIX the plan's palette (blocks x 98 float4) of each file goes to OUT_PREFIX<file name>.f32 and the units'
// first block and block count to OUT_PREFIX<file name>.blocks.tsv (for the oracle's palette comparison).
int cmdPrepare(int argc, char** argv) {
    std::string game, mods, db, exe, hf, rowsPrefix;
    std::vector<std::string> files;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--game") game = next();
        else if (a == "--mods") mods = next();
        else if (a == "--db") db = next();
        else if (a == "--exe") exe = next();
        else if (a == "--heightfield") hf = next();
        else if (a == "--rows") rowsPrefix = next();
        else if (a.size() > 1 && a[0] == '-') return usage(("unknown argument " + a).c_str());
        else files.push_back(a);
    }
    if (game.empty() || db.empty() || exe.empty() || hf.empty() || files.empty())
        return usage("prepare needs --game --db --exe --heightfield and scene files");
    c5r::gamefs::GameFiles gf;
    c5r::gamefs::GameFiles::Options opt;
    size_t at = 0;
    while (at <= mods.size() && !mods.empty()) {
        size_t sc = mods.find(';', at);
        std::string d = mods.substr(at, sc == std::string::npos ? std::string::npos : sc - at);
        if (!d.empty()) opt.extraLooseRoots.push_back(c5r::gamefs::fromUtf8(d));
        if (sc == std::string::npos) break;
        at = sc + 1;
    }
    c5r::Status s = gf.open(c5r::gamefs::fromUtf8(game), opt);
    c5m::SqliteTables tables;
    c5m::Error e;
    if (s.ok() && (!c5m::SqliteTables::available() || !tables.open(db, &e)))
        s = c5r::Status::fail(c5r::Err::Io, "database " + db + ": " + e.what);
    std::vector<uint8_t> hfb;
    c5r::heightfield::HeightGrid g;
    if (s.ok() && !readFile(hf, hfb)) s = c5r::Status::fail(c5r::Err::Io, "cannot read " + hf);
    if (s.ok()) s = c5r::heightfield::readTap(hfb.data(), hfb.size(), g);
    UnitRenderer r;
    if (s.ok()) s = r.open(nullptr, gf, tables, c5r::gamefs::fromUtf8(exe), c5r::gamefs::fromUtf8(game));
    if (!s.ok()) {
        std::fprintf(stderr, "unitscene: prepare: %s\n", s.str().c_str());
        return 1;
    }
    int rc = 0;
    for (const std::string& f : files) {
        UnitScene sc;
        SceneFileStatus fs = readSceneFile(widen(f), sc);
        if (!fs.ok()) {
            std::printf("scene %s refused %s\n", f.c_str(), fs.str().c_str());
            rc = 1;
            continue;
        }
        FramePlan plan;
        const auto t0 = std::chrono::steady_clock::now();
        c5r::Status ps = r.prepare(sc, &g, plan);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!ps.ok()) {
            std::printf("scene %s prepare failed %s\n", f.c_str(), ps.str().c_str());
            rc = 1;
            continue;
        }
        std::printf("scene %s units %zu drawn %zu blocks %u prepare_ms %.1f\n", f.c_str(), plan.units.size(),
                    plan.drawn(), plan.blocks, ms);
        std::string blocks = "player\tid\tfirst_block\tblocks\tcode\n";
        std::string meshBlocks = "player\tid\tblock\tmesh\tmesh_index\tinstance\tmember_row\tindex_count\n";
        for (const UnitPlan& u : plan.units) {
            std::string w;
            for (const std::string& x : u.status.warnings) w += (w.empty() ? "" : "; ") + x;
            std::string text = u.status.text;
            for (char& ch : text)
                if (ch == '\n' || ch == '\t') ch = ' ';
            std::printf("unit %s %d %d %s blocks %u draws %zu warnings %zu | %s%s%s\n", baseName(f).c_str(), u.player,
                        u.id, unitCodeName(u.status.code), u.blocks, u.draws.size(), u.status.warnings.size(),
                        text.c_str(), w.empty() ? "" : " | warnings: ", w.c_str());
            blocks += std::to_string(u.player) + "\t" + std::to_string(u.id) + "\t" + std::to_string(u.first_block) +
                      "\t" + std::to_string(u.blocks) + "\t" + unitCodeName(u.status.code) + "\n";
            // per block: the mesh and the living figure (index in the draw's instance order) it belongs to
            for (const MeshDraw& d : u.draws)
                for (size_t i = 0; i < d.blocks.size(); ++i)
                    meshBlocks += std::to_string(u.player) + "\t" + std::to_string(u.id) + "\t" +
                                  std::to_string(d.blocks[i]) + "\t" + (d.mesh ? d.mesh->name : std::string("?")) +
                                  "\t" + std::to_string(d.mesh ? d.mesh->index : 0) + "\t" + std::to_string(i) + "\t" +
                                  std::to_string(d.member) + "\t" +
                                  std::to_string(d.mesh ? d.mesh->indices.size() : 0) + "\n";
        }
        if (!rowsPrefix.empty()) {
            const std::string base = rowsPrefix + baseName(f);
            std::FILE* o = std::fopen((base + ".f32").c_str(), "wb");
            std::FILE* b = std::fopen((base + ".blocks.tsv").c_str(), "wb");
            std::FILE* mb = std::fopen((base + ".meshblocks.tsv").c_str(), "wb");
            bool ok = o && b && mb;
            if (ok && !plan.palette.empty())
                ok = std::fwrite(plan.palette.data(), sizeof(Float4), plan.palette.size(), o) == plan.palette.size();
            if (ok) ok = std::fwrite(blocks.data(), 1, blocks.size(), b) == blocks.size();
            if (ok) ok = std::fwrite(meshBlocks.data(), 1, meshBlocks.size(), mb) == meshBlocks.size();
            if (mb) std::fclose(mb);
            if (o) std::fclose(o);
            if (b) std::fclose(b);
            if (!ok) {
                std::fprintf(stderr, "unitscene: cannot write %s.*\n", base.c_str());
                rc = 1;
            }
        }
    }
    return rc;
}

// ------------------------------------------------------------------------------------------------ program (4406 r2)
// A pose program in text: nodes separated by ';', fields by '|': "S|clip|t" a sample, "B|a|b|w" or "B|a|b|w|mask" a
// blend of earlier nodes (mask: an index into the masks), "M|binding|default_weight|target_weight" a track mask (its
// index is its order among the M items; its bones are resolved from the model, controller_pose.h).
bool parseProgram(const std::string& text, c5r::unitanim::PoseProgram& p, std::string& why) {
    using c5r::unitanim::PoseNode;
    p.clear();
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        std::vector<std::string> f;
        std::stringstream fs(item);
        std::string x;
        while (std::getline(fs, x, '|')) f.push_back(x);
        if (f[0] == "S" && f.size() == 3) {
            PoseNode n;
            n.kind = PoseNode::Sample;
            n.clip = f[1];
            n.t = std::strtof(f[2].c_str(), nullptr);
            p.nodes.push_back(n);
        } else if (f[0] == "B" && (f.size() == 4 || f.size() == 5)) {
            PoseNode n;
            n.kind = PoseNode::Blend;
            n.a = std::atoi(f[1].c_str());
            n.b = std::atoi(f[2].c_str());
            n.w = std::strtof(f[3].c_str(), nullptr);
            n.mask = f.size() == 5 ? std::atoi(f[4].c_str()) : -1;
            p.nodes.push_back(n);
        } else if (f[0] == "M" && f.size() == 4) {
            c5r::unitanim::TrackMaskWeights m;
            m.binding = f[1];
            m.default_weight = std::strtof(f[2].c_str(), nullptr);
            m.target_weight = std::strtof(f[3].c_str(), nullptr);
            p.masks.push_back(m);
        } else {
            why = "bad program item '" + item + "'";
            return false;
        }
    }
    if (p.nodes.empty()) {
        why = "empty program";
        return false;
    }
    return true;
}

// unitscene program --list LIST.tsv --rows ROWS.f32 --out OUT.tsv [--rows-out OURS.f32] [--gsd-union GSDS.txt]
// LIST.tsv columns (tab, header first): key model model_gsd mesh clip_dir clip_gsd scale yaw_deg Tx Ty Tz offset nbind
// index_count program. Like `pose`, through poseFigureProgram (ModulePose::rowsProgram's step) instead of one clip;
// clips are clip_dir/<clip>. OUT.tsv: key status nbind rot_max_deg trans_max elem_max rot_elem_max ours_offset message
// (ours_offset: the float offset of this line's rows in OURS.f32, -1 when not written).
int cmdProgram(int argc, char** argv) {
    std::string list, rowsPath, outPath, unionPath, oursPath;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--list") list = next();
        else if (a == "--rows") rowsPath = next();
        else if (a == "--out") outPath = next();
        else if (a == "--rows-out") oursPath = next();
        else if (a == "--gsd-union") unionPath = next();
        else return usage(("unknown argument " + a).c_str());
    }
    if (list.empty() || rowsPath.empty() || outPath.empty()) return usage("--list, --rows and --out are required");
    Strings st;
    if (!unionPath.empty()) {
        std::ifstream u(unionPath);
        if (!u) {
            std::fprintf(stderr, "unitscene: cannot read %s\n", unionPath.c_str());
            return 1;
        }
        std::string l;
        while (std::getline(u, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) st.unionList.push_back(l);
        }
    }
    std::vector<uint8_t> rb;
    if (!readFile(rowsPath, rb) || rb.size() % 4) {
        std::fprintf(stderr, "unitscene: cannot read %s\n", rowsPath.c_str());
        return 1;
    }
    std::vector<float> rows(rb.size() / 4);
    if (!rows.empty()) std::memcpy(rows.data(), rb.data(), rb.size());
    std::ifstream in(list);
    if (!in) {
        std::fprintf(stderr, "unitscene: cannot read %s\n", list.c_str());
        return 1;
    }
    std::FILE* out = std::fopen(outPath.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "unitscene: cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::FILE* ours = oursPath.empty() ? nullptr : std::fopen(oursPath.c_str(), "wb");
    if (!oursPath.empty() && !ours) {
        std::fclose(out);
        std::fprintf(stderr, "unitscene: cannot write %s\n", oursPath.c_str());
        return 1;
    }
    std::fprintf(out, "key\tstatus\tnbind\trot_max_deg\ttrans_max\telem_max\trot_elem_max\tours_offset\tmessage\n");
    std::vector<std::vector<std::string>> lines;
    std::map<std::string, std::pair<std::string, std::string>> clipFiles;   // basename lower -> (path, gsd)
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> f = splitTab(line);
        if (f.size() != 15) {
            std::fprintf(stderr, "unitscene: %s: a line with %zu fields (15 wanted)\n", list.c_str(), f.size());
            std::fclose(out);
            if (ours) std::fclose(ours);
            return 1;
        }
        c5r::unitanim::PoseProgram p;
        std::string why;
        if (parseProgram(f[14], p, why))
            for (const auto& n : p.nodes)
                if (n.kind == c5r::unitanim::PoseNode::Sample)
                    clipFiles[c5m::basename_lower(n.clip)] = std::make_pair(f[4] + "/" + n.clip, f[5]);
        lines.push_back(f);
    }
    ClipCache cache([&](const std::string& name, c5m::FileInfo& fi, bool& missing, std::string& why) {
        auto it = clipFiles.find(name);
        missing = it == clipFiles.end();
        if (missing) {
            why = name + ": not in the list";
            return false;
        }
        return loadGr2(st, it->second.first, it->second.second, fi, why);
    });
    std::map<std::string, std::unique_ptr<c5m::FileInfo>> models;
    std::map<std::string, std::string> modelWhy;
    std::map<std::string, std::unique_ptr<PoseModel>> poseModels;   // per model | clip set
    long long oursAt = 0;
    size_t okCount = 0;
    for (const std::vector<std::string>& f : lines) {
        const std::string& key = f[0];
        const std::string &modelPath = f[1], &modelGsd = f[2], &meshName = f[3];
        const float s = std::strtof(f[6].c_str(), nullptr);
        const float yaw = static_cast<float>(std::strtod(f[7].c_str(), nullptr) * 3.14159265358979 / 180.0);
        const float T[3] = {std::strtof(f[8].c_str(), nullptr), std::strtof(f[9].c_str(), nullptr),
                            std::strtof(f[10].c_str(), nullptr)};
        const size_t off = std::strtoull(f[11].c_str(), nullptr, 10);
        const size_t nb = std::strtoull(f[12].c_str(), nullptr, 10);
        const size_t ic = std::strtoull(f[13].c_str(), nullptr, 10);
        auto row = [&](const char* status, const std::string& msg) {
            std::fprintf(out, "%s\t%s\t%zu\t\t\t\t\t-1\t%s\n", key.c_str(), status, nb, msg.c_str());
        };
        c5r::unitanim::PoseProgram prog;
        std::string why;
        if (!parseProgram(f[14], prog, why)) {
            row("BadInput", why);
            continue;
        }
        std::unique_ptr<c5m::FileInfo>& mf = models[modelPath];
        if (!mf && modelWhy.find(modelPath) == modelWhy.end()) {
            mf.reset(new c5m::FileInfo());
            if (!loadGr2(st, modelPath, modelGsd, *mf, why)) {
                mf.reset();
                modelWhy[modelPath] = why;
            }
        }
        if (!mf) {
            row("BadModel", modelWhy[modelPath]);
            continue;
        }
        int32_t mi = -1;
        for (size_t k = 0; k < mf->meshes.size() && mi < 0 && ic; ++k)
            if (mf->meshes[k].name == meshName && mf->meshes[k].bone_bindings.size() == nb &&
                mf->meshes[k].indices.size() == ic)
                mi = static_cast<int32_t>(k);
        for (size_t k = 0; k < mf->meshes.size() && mi < 0; ++k)
            if (mf->meshes[k].name == meshName && mf->meshes[k].bone_bindings.size() == nb)
                mi = static_cast<int32_t>(k);
        if (mi < 0) {
            row("BadInput", "mesh " + meshName + " not in " + modelPath);
            continue;
        }
        bool unresolved = false;
        for (const auto& m : prog.masks)
            if (!m.binding.empty() && m.bones.empty()) unresolved = true;
        if (unresolved) {
            c5r::unitanim::AnimStatus ms =
                c5r::unitanim::resolveTrackMasks(*mf, c5r::unitanim::meshSkeleton(*mf, static_cast<uint32_t>(mi)), prog);
            if (!ms.ok()) {
                row("TrackMaskNotBound", ms.text);
                continue;
            }
        }
        std::vector<c5m::FxsAnimation> anims;
        std::string pkey = c5m::basename_lower(modelPath);
        for (const auto& n : prog.nodes)
            if (n.kind == c5r::unitanim::PoseNode::Sample) {
                bool have = false;
                for (const auto& a : anims) have = have || c5m::basename_lower(a.file) == c5m::basename_lower(n.clip);
                if (have) continue;
                c5m::FxsAnimation a;
                a.file = n.clip;
                anims.push_back(a);
                pkey += "|" + c5m::basename_lower(n.clip);
            }
        std::unique_ptr<PoseModel>& pm = poseModels[pkey];
        if (!pm) {
            pm.reset(new PoseModel());
            PoseStatus bs = buildPoseModel(*mf, anims, pkey, *pm);
            if (!bs.ok()) {
                row(poseErrName(bs.code), bs.msg);
                pm.reset();
                continue;
            }
        }
        if (off + nb * 12 > rows.size()) {
            row("BadInput", "rows past the end of the rows file");
            continue;
        }
        std::vector<MeshPalette> mp;
        PoseStatus ps = poseFigureProgram(cache, *pm, prog, s, yaw, T,
                                          std::vector<uint32_t>(1, static_cast<uint32_t>(mi)), mp);
        if (!ps.ok()) {
            row(poseErrName(ps.code), ps.msg);
            continue;
        }
        if (mp.empty() || mp[0].rows.size() != nb) {
            row("BadInput", "the mesh binds another number of bones than the list says");
            continue;
        }
        double rmax = 0, tmax = 0, emax = 0, remax = 0;
        for (size_t k = 0; k < nb; ++k) {
            const float* cap = &rows[off + k * 12];
            const c5m::Row34& our = mp[0].rows[k];
            rmax = std::fmax(rmax, rotErrDeg(cap, our));
            double tx = cap[3] - our[3], ty = cap[7] - our[7], tz = cap[11] - our[11];
            tmax = std::fmax(tmax, std::sqrt(tx * tx + ty * ty + tz * tz));
            for (int j = 0; j < 12; ++j) {
                const double d = std::fabs(static_cast<double>(cap[j]) - our[static_cast<size_t>(j)]);
                emax = std::fmax(emax, d);
                if (j % 4 != 3) remax = std::fmax(remax, d);
            }
        }
        long long at = -1;
        if (ours) {
            bool w = true;
            for (size_t k = 0; k < nb && w; ++k)
                w = std::fwrite(mp[0].rows[k].data(), sizeof(float), 12, ours) == 12;
            if (!w) {
                std::fclose(out);
                std::fclose(ours);
                std::fprintf(stderr, "unitscene: cannot write %s\n", oursPath.c_str());
                return 1;
            }
            at = oursAt;
            oursAt += static_cast<long long>(nb * 12);
        }
        ++okCount;
        std::fprintf(out, "%s\tOk\t%zu\t%.6g\t%.6g\t%.6g\t%.6g\t%lld\t\n", key.c_str(), nb, rmax, tmax, emax, remax, at);
    }
    std::fclose(out);
    if (ours) std::fclose(ours);
    std::printf("program lines %zu ok %zu\n", lines.size(), okCount);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage("no command");
    const std::string cmd = argv[1];
    if (cmd == "scene") return cmdScene(argc, argv);
    if (cmd == "pose") return cmdPose(argc, argv);
    if (cmd == "placement") return cmdPlacement(argc, argv);
    if (cmd == "prepare") return cmdPrepare(argc, argv);
    if (cmd == "program") return cmdProgram(argc, argv);   // 4406 r2 (additive)
    return usage(("unknown command " + cmd).c_str());
}
