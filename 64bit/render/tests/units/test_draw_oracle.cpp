// test_draw_oracle.cpp - DoD B of issue 4352: the module's draw inputs against the captured ones (epic 0041).
//
// Without arguments (ctest): the check functions on hand-made inputs, positive and negative.
// With arguments: the catalogue run over a manifest written by render/evidence/0041/4352/oracle_manifest.py:
//   c5r_units_test_draw_oracle --manifest M --game INSTALL [--mods "DIR;DIR"] --db Civ5DebugDatabase.db --exe EXE
//                              --out REPORT.tsv
// Per unit: the art row by the module's rule against the index's (P10); per mesh of every member model: vertices and
// indices against the frame's draws (SHA-256, else byte by byte, ties named), material and programs, textures t0 and
// t1 by SHA-256, palette header rows (or material 87's cb0 rows 3-4) of every instance; per model the figure-major id
// rule; per frame and pass cb1 rebuilt from its camera; the environment textures t2-t5 found in the install (P1).
// Exit: 0 run complete (the report says what matched), 1 a check of the ctest part failed or an input is unusable,
// 2 usage.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/units/draw.h"
#include "c5r/units/units.h"
#include "units_testkit.h"

using namespace c5r::units;

namespace {

// ---------------------------------------------------------------------------------------------- ctest part
C5R_TEST(cb1_round_trip) {
    Cb1 c;
    for (int i = 0; i < 12; ++i) c.r[i] = {1.0f + i, 2.0f * i, -0.5f * i, i == 3 ? 1.0f : 0.25f * i};
    c.r[8] = {10.117f, 52809.3f, 5e13f, 12.5f};   // yzw not read: may differ
    c.r[11].x = c.r[3].x;
    c.r[11].y = c.r[3].y;
    c.r[11].z = c.r[3].z;
    Cb1 b = makeCb1(cameraFromCb1(c));
    std::string why;
    CHECK(cb1ReadLanesEqual(c, b, &why));
    b.r[5].y += 1e-3f;
    CHECK(!cb1ReadLanesEqual(c, b, &why));
    CHECK_EQ(why, std::string("r4-r7 (view-projection)"));
    b = makeCb1(cameraFromCb1(c));
    b.r[8].x = 0;
    CHECK(!cb1ReadLanesEqual(c, b, &why));
}

C5R_TEST(buffer_compare) {
    ModelMesh m;
    m.stride = 4;
    m.vertices = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::vector<uint8_t> cap = m.vertices;
    BufferCompare c = compareVertices(m, cap.data(), cap.size());
    CHECK(c.all());
    cap[5] ^= 1;   // vertex 1
    c = compareVertices(m, cap.data(), cap.size());
    CHECK(!c.all());
    CHECK_EQ(c.equal, 2u);
    CHECK(c.differ == std::vector<uint32_t>{1});
    cap[9] ^= 1;   // vertex 2
    c = compareVertices(m, cap.data(), cap.size());
    CHECK_EQ(c.equal, 1u);
    CHECK((c.differ == std::vector<uint32_t>{1, 2}));
    c = compareVertices(m, cap.data(), cap.size() - 4);
    CHECK(!c.sizeMatch);
    m.indices = {0, 1, 2};
    std::vector<uint8_t> ib = {0, 0, 1, 0, 2, 0};
    CHECK(compareIndices(m, ib.data(), ib.size()).all());
    ib[4] = 3;
    CHECK(!compareIndices(m, ib.data(), ib.size()).all());
}

C5R_TEST(header_and_ids) {
    Float4 ours[2];
    const float colour[4] = {217.0f / 255, 88.0f / 255, 0, 1};
    paletteHeader(colour, 1, 0, 0, ours);
    float cap[8] = {0.851f, 0.3451f, 0, 1, 1, 0, 0, -692.0f};   // w of row 1 is not read
    float d;
    CHECK(headerRowsEqual(ours, cap, &d));
    cap[1] = 0.36f;
    CHECK(!headerRowsEqual(ours, cap, &d));
    cap[1] = 0.3451f;
    cap[6] = 0.5f;   // a fade
    CHECK(!headerRowsEqual(ours, cap, &d));
    std::string why;
    CHECK(idsFigureMajor({{21, 23, 19}, {22, 24, 20}}, 3, &why));
    CHECK(idsFigureMajor({{0, 1, 2}}, 3, &why));
    CHECK(!idsFigureMajor({{21, 23, 19}, {22, 24, 26}}, 3, &why));
    CHECK(!idsFigureMajor({{21, 23}, {22, 24}}, 3, &why));
    Float4 cb[16];
    Float4 world[3];
    const float T[3] = {320, -499.8f, 9.03f};
    yawRows(0.5235988f, T, world);
    staticCb0(colour, 1, 0, 0, world, cb);
    CHECK_EQ(cb[0].w, 320.0f);
    CHECK(std::fabs(cb[0].x - 0.866025f) < 1e-6f);
    CHECK(std::fabs(cb[0].y + 0.5f) < 1e-6f);
    CHECK_EQ(cb[3].y, 88.0f / 255);
    CHECK_EQ(cb[4].x, 1.0f);
    CHECK_EQ(cb[15].x, 0.0f);
}

// ---------------------------------------------------------------------------------------------- catalogue run
struct DrawRec {
    std::string frame, pass, vs, ps, vb, vbSha, ib, ibSha, ids, palette, cb1, cb0, t0, t1, env;
    int n = 0, stride = 0, indexCount = 0, instances = 0;
};
struct UnitRec {
    std::string frame, object, type, artRow, civ, era, colour;
    int x = 0, y = 0;
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> o;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            o.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    o.push_back(cur);
    return o;
}

// The file's ArtToolInfo for the rounding analysis (4356): units per metre and the right / up / back axes, as hex
// float bits, plus the tool and exporter names.
std::string artTool(const c5m::FileInfo& f) {
    std::string s;
    char b[16];
    auto hx = [&](float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        std::snprintf(b, sizeof b, "%08x", u);
        return std::string(b);
    };
    s = "upm=" + hx(f.units_per_meter) + " r=";
    for (int i = 0; i < 3; ++i) s += hx(f.right[i]) + (i < 2 ? "," : "");
    s += " u=";
    for (int i = 0; i < 3; ++i) s += hx(f.up[i]) + (i < 2 ? "," : "");
    s += " b=";
    for (int i = 0; i < 3; ++i) s += hx(f.back[i]) + (i < 2 ? "," : "");
    s += " o=";
    for (int i = 0; i < 3; ++i) s += hx(f.origin[i]) + (i < 2 ? "," : "");
    return s + " tool=" + f.art_tool + " exporter=" + f.exporter;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(c5r::gamefs::fromUtf8(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

int runCatalogue(int argc, char** argv) {
    std::map<std::string, std::string> a;
    for (int i = 1; i + 1 < argc; i += 2) a[argv[i]] = argv[i + 1];
    for (const char* k : {"--manifest", "--game", "--db", "--exe", "--out"})
        if (!a.count(k)) {
            std::fprintf(stderr, "usage: --manifest M --game DIR [--mods \"A;B\"] --db DB --exe EXE --out R (missing %s)\n", k);
            return 2;
        }
    std::map<std::string, std::string> frames;
    std::vector<UnitRec> units;
    std::map<std::string, std::vector<DrawRec>> draws;
    {
        std::ifstream m(c5r::gamefs::fromUtf8(a["--manifest"]));
        if (!m) return std::fprintf(stderr, "no manifest\n"), 1;
        std::string line;
        while (std::getline(m, line)) {
            auto f = split(line, '\t');
            if (f[0] == "frame" && f.size() >= 3) frames[f[1]] = f[2];
            if (f[0] == "unit" && f.size() >= 10) {
                UnitRec u;
                u.frame = f[1]; u.object = f[2]; u.type = f[3]; u.artRow = f[4]; u.civ = f[5]; u.era = f[6];
                u.colour = f[7]; u.x = std::atoi(f[8].c_str()); u.y = std::atoi(f[9].c_str());
                units.push_back(u);
            }
            if (f[0] == "draw" && f.size() >= 21) {
                DrawRec d;
                d.frame = f[1]; d.n = std::atoi(f[2].c_str()); d.pass = f[3]; d.vs = f[4]; d.ps = f[5];
                d.stride = std::atoi(f[6].c_str()); d.vb = f[7]; d.vbSha = f[8]; d.ib = f[9]; d.ibSha = f[10];
                d.indexCount = std::atoi(f[11].c_str()); d.instances = std::atoi(f[12].c_str()); d.ids = f[14];
                d.palette = f[15]; d.cb1 = f[16]; d.cb0 = f[17]; d.t0 = f[18]; d.t1 = f[19]; d.env = f[20];
                draws[d.frame].push_back(d);
            }
        }
    }
    c5r::gamefs::GameFiles gf;
    c5r::gamefs::GameFiles::Options opt;
    if (a.count("--mods"))
        for (auto& d : split(a["--mods"], ';'))
            if (!d.empty()) opt.extraLooseRoots.push_back(c5r::gamefs::fromUtf8(d));
    c5r::Status s = gf.open(c5r::gamefs::fromUtf8(a["--game"]), opt);
    if (!s.ok()) return std::fprintf(stderr, "game files: %s\n", s.str().c_str()), 1;
    c5m::SqliteTables db;
    c5m::Error e;
    if (!db.open(a["--db"], &e)) return std::fprintf(stderr, "db: %s\n", e.what.c_str()), 1;
    GameFilesProvider prov(gf);
    ArtResolver art;
    UnitStatus us = art.open(db, prov);
    if (!us.ok()) return std::fprintf(stderr, "art: %s\n", us.str().c_str()), 1;
    c5m::StringDatabase strings;
    us = loadStringDatabases(gf.names(), prov, strings);
    if (!us.ok()) return std::fprintf(stderr, "strings: %s\n", us.str().c_str()), 1;
    ModelCache cache(prov);
    cache.setStrings(&strings);
    UnitPrograms progs;
    s = progs.open(c5r::gamefs::fromUtf8(a["--exe"]));
    if (!s.ok()) return std::fprintf(stderr, "programs: %s\n", s.str().c_str()), 1;

    std::ofstream out(c5r::gamefs::fromUtf8(a["--out"]), std::ios::binary);
    out << "# 4352 draw-input oracle; manifest " << a["--manifest"] << "\n";
    out << "#mesh\tframe\tobject\tart_row\tmember\tgr2\tmesh\tmaterial\tvertices\tties\tdraws(mask/refl/scene)\tvb\tib\t"
           "t0\tt1\tprograms\theader_ok/checked\tdiffer\n";
    std::map<std::string, std::vector<uint8_t>> fileCache;
    auto bytesOf = [&](const std::string& frame, const std::string& rel) -> const std::vector<uint8_t>* {
        const std::string p = frames[frame] + "/" + rel;
        auto it = fileCache.find(p);
        if (it != fileCache.end()) return &it->second;
        std::vector<uint8_t> b;
        if (!readFile(p, b)) return nullptr;
        if (fileCache.size() > 256) fileCache.clear();
        return &(fileCache[p] = std::move(b));
    };
    struct Totals { int units = 0, p10 = 0, p10ok = 0, loaded = 0, meshes = 0, vbExact = 0, vbTies = 0, vbDiffer = 0,
                    absent = 0, ibOk = 0, t0ok = 0, t1ok = 0, t1none = 0, progOk = 0, headerOk = 0, headerChecked = 0,
                    idModels = 0, idOk = 0; } t;
    std::map<std::string, int> loadFail;
    for (auto& u : units) {
        ++t.units;
        std::string row;
        UnitStatus rs = art.art_row(u.type, u.civ, u.era, row);
        ++t.p10;
        const bool p10 = rs.ok() && row == u.artRow;
        t.p10ok += p10 ? 1 : 0;
        out << "unit\t" << u.frame << "\t" << u.object << "\t" << u.type << "\t" << u.artRow << "\t"
            << (rs.ok() ? row : rs.str()) << "\t" << (p10 ? "P10ok" : "P10differs") << "\n";
        UnitArt ua;
        UnitStatus st = art.resolve(u.artRow, ua);
        float colour[4] = {0, 0, 0, 1};
        UnitStatus cs = art.team_colour(u.colour, colour);
        std::vector<const UnitModel*> models;
        for (size_t mi = 0; st.ok() && mi < ua.members.size(); ++mi) {
            const UnitModel* m = nullptr;
            st = cache.get(ua.members[mi], m);
            models.push_back(m);
        }
        if (!st.ok() || !cs.ok()) {
            out << "unitfail\t" << u.frame << "\t" << u.object << "\t" << u.artRow << "\t" << (st.ok() ? cs.str() : st.str()) << "\n";
            ++loadFail[unitCodeName(st.ok() ? cs.code : st.code)];
            continue;
        }
        ++t.loaded;
        const auto& fd = draws[u.frame];
        for (size_t mi = 0; mi < ua.members.size(); ++mi) {
            const UnitModel* m = models[mi];
            std::vector<std::vector<uint32_t>> sceneIds;
            bool allSkinnedSeen = true;
            for (auto& mesh : m->meshes) {
                ++t.meshes;
                const std::string vsha = sha256Hex(mesh.vertices.data(), mesh.vertices.size());
                const std::string isha = sha256Hex(reinterpret_cast<const uint8_t*>(mesh.indices.data()), mesh.indices.size() * 2);
                std::vector<const DrawRec*> hit;
                for (auto& d : fd)
                    if (d.vbSha == vsha) hit.push_back(&d);
                std::string vbRes = "exact", differ;
                if (hit.empty()) {
                    // same size and stride and index count: compare bytes
                    for (auto& d : fd) {
                        if (d.stride != static_cast<int>(mesh.stride) || d.indexCount != static_cast<int>(mesh.indices.size())) continue;
                        const auto* cap = bytesOf(u.frame, d.vb);
                        if (!cap || cap->size() != mesh.vertices.size()) continue;
                        const auto* ib = bytesOf(u.frame, d.ib);
                        if (!ib || !compareIndices(mesh, ib->data(), ib->size()).all()) continue;
                        BufferCompare c = compareVertices(mesh, cap->data(), cap->size());
                        if (c.equal * 2 < c.elements) continue;
                        hit.push_back(&d);
                        // 0450: differOnlyAtTies removed (0447 left no tie list); "ties only" in the summary stays 0
                        vbRes = "differ:" + std::to_string(c.elements - c.equal);
                        differ.clear();
                        // vertex:element:ours>captured for the first 8 (element: p position, w weights, i indices,
                        // n normal, t uv; values as hex half / byte)
                        int shown = 0;
                        for (uint32_t v : c.differ) {
                            differ += (differ.empty() ? "" : ",") + std::to_string(v);
                            if (shown++ >= 8 || mesh.stride != kSkinnedStride) continue;
                            const uint8_t* o = mesh.vertices.data() + static_cast<size_t>(v) * 28;
                            const uint8_t* q = cap->data() + static_cast<size_t>(v) * 28;
                            static const struct { int off, len; char tag; bool half; } el[] = {
                                {0, 8, 'p', true}, {8, 4, 'w', false}, {12, 4, 'i', false}, {16, 8, 'n', true}, {24, 4, 't', true}};
                            for (auto& x : el)
                                for (int k = 0; k < x.len; k += x.half ? 2 : 1) {
                                    const unsigned ov = x.half ? (o[x.off + k] | (o[x.off + k + 1] << 8)) : o[x.off + k];
                                    const unsigned qv = x.half ? (q[x.off + k] | (q[x.off + k + 1] << 8)) : q[x.off + k];
                                    if (ov == qv) continue;
                                    char b[96];
                                    std::snprintf(b, sizeof b, ":%c%d=%x>%x", x.tag, x.half ? k / 2 : k, ov, qv);
                                    differ += b;
                                    // the source float (p, n) for the rounding analysis (unit-renderer.md 10.2)
                                    const c5m::Mesh& sm = m->file->meshes[mesh.index];
                                    const c5m::VertexComponent* sc =
                                        sm.format.find(x.tag == 'p' ? "Position" : x.tag == 'n' ? "Normal" : "");
                                    if (sc && sc->type == c5m::MemberType::Real32 && k / 2 < 3) {
                                        uint32_t bits;
                                        std::memcpy(&bits, sm.vertices.data() + static_cast<size_t>(v) * sm.format.stride +
                                                               sc->offset + 4 * (k / 2), 4);
                                        std::snprintf(b, sizeof b, "(src %08x s %08x)", bits, *reinterpret_cast<const uint32_t*>(&m->scale));
                                        differ += b;
                                    }
                                }
                        }
                    }
                }
                if (hit.empty()) vbRes = "absent";
                if (vbRes == "exact") ++t.vbExact;
                else if (vbRes.compare(0, 5, "ties:") == 0) ++t.vbTies;
                else if (vbRes == "absent") ++t.absent;
                else ++t.vbDiffer;
                int perPass[3] = {0, 0, 0};
                bool ibOk = !hit.empty(), t0ok = !hit.empty(), t1ok = !hit.empty(), progOk = !hit.empty();
                int hOk = 0, hChecked = 0;
                // Textures: a vertex buffer can be shared by two units of the frame (archer and barbarian archer draw
                // the same mesh with their own textures), so a mesh's t0 / t1 are equal when one of its draws binds
                // ours (4356); the first non-mask draw is logged when none does.
                const std::string ourT0 = mesh.t0 ? sha256Hex(mesh.t0->payload.data(), mesh.t0->payload.size()) : "-";
                const std::string ourT1 = mesh.t1 ? sha256Hex(mesh.t1->payload.data(), mesh.t1->payload.size()) : "-";
                const DrawRec* texDraw = nullptr;
                bool t0any = false, t1any = false;
                for (auto* d : hit)
                    if (d->pass != "mask") {
                        if (!texDraw) texDraw = d;
                        t0any = t0any || ourT0 == d->t0;
                        t1any = t1any || ourT1 == d->t1;
                    }
                if (texDraw) {
                    t0ok = t0any;
                    t1ok = t1any;
                }
                const std::vector<uint32_t>* sceneDraw = nullptr;
                std::vector<uint32_t> sceneList;
                for (auto* d : hit) {
                    const PassKind pk = d->pass == "mask" ? PassKind::ShadowMask
                                        : d->pass == "reflection" ? PassKind::Reflection : PassKind::Scene;
                    ++perPass[static_cast<int>(pk)];
                    if (d->ibSha != isha) ibOk = false;
                    if (d == texDraw && (!t0ok || !t1ok)) {
                        {
                            // for the binding analysis (4356): what we bound, what the capture bound, the art's names
                            std::string fxs;
                            for (auto& ft : ua.members[mi].files.textures) fxs += (fxs.empty() ? "" : ",") + ft.name;
                            std::string maps;
                            const c5m::Mesh& sm = m->file->meshes[mesh.index];
                            for (int32_t mx : sm.materials)
                                if (mx >= 0 && static_cast<size_t>(mx) < m->file->materials.size())
                                {
                                    const c5m::Material& mt = m->file->materials[static_cast<size_t>(mx)];
                                    maps += (maps.empty() ? "" : ";") + std::string("[") + mt.name + " tex=" +
                                            mt.texture_file + "]";
                                    for (auto& mp : mt.maps) maps += "," + mp.usage + "=" + mp.basename;
                                    maps += " ext{set=" + mt.shader_set;
                                    for (auto& kv : mt.extended) maps += " " + kv.first + "=" + kv.second;
                                    maps += "}";
                                }
                            maps += " root:";
                            for (auto& rt : m->file->textures) maps += " " + rt;
                            maps += " mats:" + std::to_string(m->file->materials.size()) + " meshmats:" +
                                    std::to_string(sm.materials.size());
                            out << "texne\t" << u.frame << "\t" << u.artRow << "\t" << m->gr2 << "\t" << mesh.name << "\t"
                                << mesh.base_texture << "\t" << d->t0 << "\t" << (t0ok ? "eq" : "NE") << "\t"
                                << mesh.sref_texture << "\t" << d->t1 << "\t" << (t1ok ? "eq" : "NE") << "\t"
                                << (fxs.empty() ? "-" : fxs) << "\t" << (maps.empty() ? "-" : maps) << "\n";
                        }
                    }
                    if (progs.sha256(mesh.material, pk, true) != d->vs || progs.sha256(mesh.material, pk, false) != d->ps)
                        progOk = false;
                    Float4 head[2];
                    paletteHeader(colour, 1, 0, 0, head);
                    if (mesh.material == Material::Static && d->cb0 != "-") {
                        const auto* cb = bytesOf(u.frame, d->cb0);
                        if (cb && cb->size() >= 80) {
                            float cap[8];
                            std::memcpy(cap, cb->data() + 48, 32);
                            float dd;
                            ++hChecked;
                            hOk += headerRowsEqual(head, cap, &dd) ? 1 : 0;
                        }
                    } else if (d->palette != "-" && d->ids != "-") {
                        const auto* pal = bytesOf(u.frame, d->palette);
                        std::vector<uint32_t> ids;
                        for (auto& x : split(d->ids, ',')) ids.push_back(static_cast<uint32_t>(std::atoi(x.c_str())));
                        for (uint32_t id : ids) {
                            if (!pal || (static_cast<size_t>(id) * kPaletteRows + 2) * 16 > pal->size()) continue;
                            float cap[8];
                            std::memcpy(cap, pal->data() + static_cast<size_t>(id) * kPaletteRows * 16, 32);
                            float dd;
                            ++hChecked;
                            hOk += headerRowsEqual(head, cap, &dd) ? 1 : 0;
                        }
                        if (pk == PassKind::Scene && !sceneDraw) {
                            sceneList = ids;
                            sceneDraw = &sceneList;
                        }
                    }
                }
                if (mesh.material != Material::Static) {
                    if (sceneDraw) sceneIds.push_back(*sceneDraw);
                    else allSkinnedSeen = false;
                }
                if (!hit.empty()) {
                    t.ibOk += ibOk; t.t0ok += t0ok; t.t1ok += t1ok; t.progOk += progOk;
                    t.t1none += mesh.t1 ? 0 : 1;
                }
                t.headerOk += hOk;
                t.headerChecked += hChecked;
                out << "mesh\t" << u.frame << "\t" << u.object << "\t" << u.artRow << "\t" << ua.members[mi].member << "\t"
                    << m->gr2 << "\t" << mesh.name << "\t" << static_cast<int>(mesh.material) << "\t" << mesh.vertex_count
                    << "\t" << mesh.tie_vertices.size() << "\t" << perPass[0] << "/" << perPass[1] << "/" << perPass[2]
                    << "\t" << vbRes << "\t" << (hit.empty() ? "-" : ibOk ? "eq" : "NE") << "\t"
                    << (hit.empty() ? "-" : t0ok ? "eq" : "NE") << "\t" << (hit.empty() ? "-" : t1ok ? "eq" : "NE")
                    << "\t" << (hit.empty() ? "-" : progOk ? "eq" : "NE") << "\t" << hOk << "/" << hChecked << "\t"
                    << (differ.empty() ? "-" : differ) << (m->y_up ? "\tbasis-turned" : "\t-") << "\t" << artTool(*m->file)
                    << "\n";
            }
            if (allSkinnedSeen && sceneIds.size() >= 1) {
                ++t.idModels;
                std::string why;
                const bool ok = idsFigureMajor(sceneIds, static_cast<uint32_t>(sceneIds[0].size()), &why);
                t.idOk += ok ? 1 : 0;
                if (!ok) out << "ids\t" << u.frame << "\t" << u.object << "\t" << m->gr2 << "\t" << why << "\n";
            }
        }
    }
    // cb1 per frame and pass
    int cbFrames = 0, cbOk = 0;
    std::set<std::string> envShas;
    for (auto& fr : draws) {
        std::set<std::string> passSeen;
        for (auto& d : fr.second) {
            for (auto& x : split(d.env, ','))
                if (x != "-") envShas.insert(x);
            if (passSeen.count(d.pass) || d.cb1 == "-") continue;
            passSeen.insert(d.pass);
            const auto* b = bytesOf(fr.first, d.cb1);
            if (!b || b->size() < kCb1Bytes) continue;
            Cb1 cap;
            std::memcpy(&cap, b->data(), kCb1Bytes);
            std::string why;
            const bool ok = cb1ReadLanesEqual(cap, makeCb1(cameraFromCb1(cap)), &why);
            ++cbFrames;
            cbOk += ok ? 1 : 0;
            char thr[64];
            std::snprintf(thr, sizeof thr, "%.9g", cap.r[8].x);
            out << "cb1\t" << fr.first << "\t" << d.pass << "\t" << (ok ? "eq" : why) << "\tthreshold " << thr << "\n";
        }
    }
    // P1: the environment textures in the install
    std::map<std::string, std::string> envFound;
    for (auto& n : gf.names()) {
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".dds") != 0 || gf.size(n) > 200000) continue;
        std::vector<uint8_t> b;
        if (!gf.read(n, b).ok()) continue;
        c5r::gamefs::DdsImage img;
        if (!c5r::gamefs::parseDds(b.data(), b.size(), img).ok()) continue;
        std::vector<uint8_t> payload;
        for (auto& sr : img.subresources) payload.insert(payload.end(), img.pixels() + sr.offset, img.pixels() + sr.offset + sr.size);
        if (img.format == DXGI_FORMAT_B8G8R8A8_UNORM)   // as ModelCache::texture keeps it (R8G8B8A8 order)
            for (size_t i = 0; i + 3 < payload.size(); i += 4) std::swap(payload[i], payload[i + 2]);
        const std::string h = sha256Hex(payload.data(), payload.size());
        if (envShas.count(h)) envFound[h] += (envFound[h].empty() ? "" : ",") + n + "<-" + gf.origin(n);
    }
    for (auto& h : envShas) out << "env\t" << h << "\t" << (envFound.count(h) ? envFound[h] : "NOT IN INSTALL") << "\n";
    char sum[1024];
    std::snprintf(sum, sizeof sum,
                  "units %d; P10 art row equal %d/%d; units loaded %d; meshes %d: vb exact %d, ties only %d, differ %d, "
                  "absent %d; ib eq %d; t0 eq %d; t1 eq %d (meshes without t1 %d); programs eq %d; header rows %d/%d; "
                  "figure-major id models %d/%d; cb1 read lanes %d/%d; env textures %zu, found in install %zu",
                  t.units, t.p10ok, t.p10, t.loaded, t.meshes, t.vbExact, t.vbTies, t.vbDiffer, t.absent, t.ibOk, t.t0ok,
                  t.t1ok, t.t1none, t.progOk, t.headerOk, t.headerChecked, t.idOk, t.idModels, cbOk, cbFrames,
                  envShas.size(), envFound.size());
    out << "summary\t" << sum << "\n";
    for (auto& kv : loadFail) out << "loadfail\t" << kv.first << "\t" << kv.second << "\n";
    std::printf("%s\n", sum);
    for (auto& kv : loadFail) std::printf("load failures %s: %d\n", kv.first.c_str(), kv.second);
    return 0;
}

// --draw: the module's own draws of chosen captured draws (DoD C). Our meshes (matched by vertex-buffer SHA-256, else by
// vertex count and stride), our textures, programs, states and environment textures; the captured palette, ids, cb1
// and (material 87) cb0, so that only the draw path is under test. Target: R8G8B8A8_UNORM W x H cleared to 0, depth
// D32 cleared to 1 (as terrainreplay), hardware device; written as raw RGBA8 to --out-rgba.
int runDraw(int argc, char** argv) {
    std::map<std::string, std::string> a;
    for (int i = 2; i + 1 < argc; i += 2) a[argv[i]] = argv[i + 1];
    for (const char* k : {"--manifest", "--frame", "--draws", "--art", "--game", "--db", "--exe", "--out-rgba", "--width", "--height"})
        if (!a.count(k)) return std::fprintf(stderr, "--draw: missing %s\n", k), 2;
    std::string exportDir;
    std::map<int, DrawRec> recs;
    {
        std::ifstream m(c5r::gamefs::fromUtf8(a["--manifest"]));
        std::string line;
        while (std::getline(m, line)) {
            auto f = split(line, '\t');
            if (f[0] == "frame" && f.size() >= 3 && f[1] == a["--frame"]) exportDir = f[2];
            if (f[0] == "draw" && f.size() >= 21 && f[1] == a["--frame"]) {
                DrawRec d;
                d.n = std::atoi(f[2].c_str()); d.pass = f[3]; d.stride = std::atoi(f[6].c_str()); d.vbSha = f[8];
                d.indexCount = std::atoi(f[11].c_str()); d.ids = f[14]; d.palette = f[15]; d.cb1 = f[16]; d.cb0 = f[17];
                recs[d.n] = d;
            }
        }
    }
    if (exportDir.empty()) return std::fprintf(stderr, "--draw: frame not in the manifest\n"), 1;
    c5r::gamefs::GameFiles gf;
    c5r::gamefs::GameFiles::Options opt;
    if (a.count("--mods"))
        for (auto& d : split(a["--mods"], ';'))
            if (!d.empty()) opt.extraLooseRoots.push_back(c5r::gamefs::fromUtf8(d));
    c5r::Status s = gf.open(c5r::gamefs::fromUtf8(a["--game"]), opt);
    if (!s.ok()) return std::fprintf(stderr, "game files: %s\n", s.str().c_str()), 1;
    c5m::SqliteTables db;
    c5m::Error e;
    if (!db.open(a["--db"], &e)) return std::fprintf(stderr, "db: %s\n", e.what.c_str()), 1;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, a["--device"] == "warp" ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                                 nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
        return std::fprintf(stderr, "no device\n"), 1;
    UnitRenderer r;
    s = r.open(dev, gf, db, c5r::gamefs::fromUtf8(a["--exe"]), c5r::gamefs::fromUtf8(a["--game"]));
    if (!s.ok()) return std::fprintf(stderr, "open: %s\n", s.str().c_str()), 1;
    UnitInput ui;
    ui.art_row = a["--art"];
    UnitArt art;
    std::vector<const UnitModel*> models;
    UnitStatus us = r.loadUnit(ui, nullptr, art, models);
    if (!us.ok()) return std::fprintf(stderr, "unit: %s\n", us.str().c_str()), 1;
    FramePlan plan;
    plan.units.emplace_back();
    UnitPlan& up = plan.units.back();
    std::string pass;
    std::string paletteFile, cb1File;
    for (auto& ns : split(a["--draws"], ',')) {
        auto it = recs.find(std::atoi(ns.c_str()));
        if (it == recs.end()) return std::fprintf(stderr, "draw %s not in the manifest\n", ns.c_str()), 1;
        const DrawRec& d = it->second;
        pass = d.pass;
        cb1File = d.cb1;
        const ModelMesh* hit = nullptr;
        for (auto* m : models)
            for (auto& mm : m->meshes)
                if (!hit && sha256Hex(mm.vertices.data(), mm.vertices.size()) == d.vbSha) hit = &mm;
        for (auto* m : models)
            for (auto& mm : m->meshes)
                if (!hit && static_cast<int>(mm.stride) == d.stride && static_cast<int>(mm.indices.size()) == d.indexCount)
                    hit = &mm;
        if (!hit) return std::fprintf(stderr, "draw %d: no mesh of %s matches\n", d.n, ui.art_row.c_str()), 1;
        MeshDraw md;
        md.mesh = hit;
        md.material = hit->material;
        if (hit->material == Material::Static) {
            std::vector<uint8_t> cb;
            if (d.cb0 == "-" || !readFile(exportDir + "/" + d.cb0, cb) || cb.size() < kStaticCb0Bytes)
                return std::fprintf(stderr, "draw %d: no cb0\n", d.n), 1;
            md.cb0.resize(16);
            std::memcpy(md.cb0.data(), cb.data(), kStaticCb0Bytes);
        } else {
            for (auto& x : split(d.ids, ',')) md.blocks.push_back(static_cast<uint32_t>(std::atoi(x.c_str())));
            paletteFile = d.palette;
        }
        up.draws.push_back(md);
    }
    if (!paletteFile.empty()) {
        std::vector<uint8_t> pal;
        if (!readFile(exportDir + "/" + paletteFile, pal)) return std::fprintf(stderr, "no palette\n"), 1;
        plan.palette.resize(pal.size() / 16);
        std::memcpy(plan.palette.data(), pal.data(), plan.palette.size() * 16);
        plan.blocks = static_cast<uint32_t>(plan.palette.size() / kPaletteRows);
    }
    std::vector<uint8_t> cb1b;
    if (!readFile(exportDir + "/" + cb1File, cb1b) || cb1b.size() < kCb1Bytes) return std::fprintf(stderr, "no cb1\n"), 1;
    const UINT W = static_cast<UINT>(std::atoi(a["--width"].c_str())), H = static_cast<UINT>(std::atoi(a["--height"].c_str()));
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W;
    td.Height = H;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *tex = nullptr, *depth = nullptr, *staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    dev->CreateTexture2D(&td, nullptr, &tex);
    dev->CreateRenderTargetView(tex, nullptr, &rtv);
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dev->CreateTexture2D(&td, nullptr, &depth);
    dev->CreateDepthStencilView(depth, nullptr, &dsv);
    if (!rtv || !dsv) return std::fprintf(stderr, "targets\n"), 1;
    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    PassSet ps;
    PassDesc p;
    p.kind = pass == "mask" ? PassKind::ShadowMask : pass == "reflection" ? PassKind::Reflection : PassKind::Scene;
    p.rtv = rtv;
    p.dsv = dsv;
    p.viewport = {0, 0, static_cast<float>(W), static_cast<float>(H), 0, 1};
    std::memcpy(&p.cb1, cb1b.data(), kCb1Bytes);
    ps.passes.push_back(p);
    s = r.draw(ctx, plan, ps);
    if (!s.ok()) return std::fprintf(stderr, "draw: %s\n", s.str().c_str()), 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &staging);
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE mp;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mp))) return std::fprintf(stderr, "map\n"), 1;
    std::ofstream o(c5r::gamefs::fromUtf8(a["--out-rgba"]), std::ios::binary);
    for (UINT y = 0; y < H; ++y) o.write(static_cast<const char*>(mp.pData) + static_cast<size_t>(y) * mp.RowPitch, W * 4);
    ctx->Unmap(staging, 0);
    std::printf("drew %zu draws (%s pass) of %s into %ux%u\n", up.draws.size(), pass.c_str(), ui.art_row.c_str(), W, H);
    staging->Release(); dsv->Release(); depth->Release(); rtv->Release(); tex->Release(); ctx->Release(); dev->Release();
    return o ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--draw") return runDraw(argc, argv);
    if (argc > 1) return runCatalogue(argc, argv);
    return ::c5r::test::runAll();
}
