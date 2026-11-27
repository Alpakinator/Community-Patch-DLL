// render/tools/watershot/watershot.cpp - the water draws of a stock capture replayed and compared, and the module's
// water at a live camera (epic 3600, issue 3604). Own code. Usage:
//
//   watershot replay EXPORT --exe EXE [--game DIR] [--system ocean|all] [--module] [--gt DIR | --tap FILE]
//                            [--out DIR] [--warp]
//       every draw of the system (default ocean) with the capture's own inputs; with --module, the module's WaterFrame
//       and ocean mesh in place of the capture's (needs --game for the stock fields). Per draw: the share of pixels
//       equal to the capture's "after" target and the distribution of the rest; with --module also the mesh check.
//   watershot frame EXPORT --exe EXE --game DIR [--gt DIR | --tap FILE]
//       the captured WaterFrame of each view against computeWaterFrame, member by member.
//   watershot framefile BLOCK.bin --exe EXE --game DIR [--gt DIR | --tap FILE]
//       the same check for one raw 0x250-byte WaterFrame block (an ocean draw's cb1 file of an export).
//   watershot live --camera CAM.json (--gt DIR | --tap FILE) --exe EXE [--game DIR] [--view I] [--pass P]
//                  [--reflect RRGGBB] [--out PNG] [--warp]
//       the module's ocean at a ground-truth camera (epic 3000/3200 camera JSON), over a cleared target (diagnostic
//       picture; the terrain is not drawn).
//
// Exit codes: 0 report written (differences are results, not failures), 2 usage, 4 an input missing or unparsable,
// 6 device or set-up. EXPORT, the EXE and the install are read-only; pictures go only to --out (scratch, D83).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/water/water.h"
#include "terrainshot_core.h"
#include "watershot_core.h"
#include "c5r/gamefs/dds.h"
#include "c5r/terrainsets/lean.h"

using namespace c5r;

namespace {

// The exporter names systems with a pass suffix ("ocean_p3", "river_p4").
bool isOcean(const std::string& s) { return s == "ocean" || s.rfind("ocean_", 0) == 0; }
bool isRiver(const std::string& s) { return s == "river" || s.rfind("river_", 0) == 0; }

// All subresource bytes of a captured texture, in order.
std::vector<uint8_t> allBytes(const watershot::TexFile& t) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < t.subs.size(); ++i) {
        const watershot::TexFile::Sub& x = t.subs[i];
        const uint32_t m = static_cast<uint32_t>(i % t.mips);
        const size_t n = static_cast<size_t>(x.slicePitch) * std::max(1u, t.depth >> m);
        if (x.offset + n <= t.bytes.size()) out.insert(out.end(), t.bytes.begin() + x.offset, t.bytes.begin() + x.offset + n);
    }
    return out;
}

// The module's river inputs against the capture's river draw, slot by slot (3605): the .lean blocks, the two DDS
// files, River+0x24 (zero), the Rivers ColorKey of style Generic, and the own buffer's World.
std::string riverInputs(const watershot::CapturedDraw& s, const water::RiverArt& art, bool have) {
    if (!have) return "river inputs: no install given (--game)";
    auto srvOf = [&](uint32_t slot) -> const watershot::TexFile* {
        for (const watershot::Stage::Srv& v : s.ps.srvs)
            if (v.slot == slot) return &v.tex;
        return nullptr;
    };
    std::string r = "river inputs:";
    auto cmp = [&](uint32_t slot, const uint8_t* want, size_t n) {
        const watershot::TexFile* t = srvOf(slot);
        if (!t) {
            r += " t" + std::to_string(slot) + " not in the export;";
            return;
        }
        const std::vector<uint8_t> got = allBytes(*t);
        const bool eq = got.size() == n && std::memcmp(got.data(), want, n) == 0;
        size_t diff = 0;
        for (size_t i = 0; i < std::min(n, got.size()); ++i) diff += got[i] != want[i];
        r += " t" + std::to_string(slot) + (eq ? " equal" : " DIFFERS (" + std::to_string(diff) + " of " + std::to_string(n) + " bytes, sizes " +
                                                        std::to_string(got.size()) + " / " + std::to_string(n) + ")") + ";";
    };
    for (int k = 0; k < 2; ++k) {
        terrainsets::LeanFile lf;
        if (!terrainsets::parseLean(art.leanBytes[k].data(), art.leanBytes[k].size(), lf).ok()) {
            r += " lean " + std::to_string(k) + " unparsable;";
            continue;
        }
        cmp(static_cast<uint32_t>(2 * k), lf.four, lf.fourBytes);
        cmp(static_cast<uint32_t>(2 * k + 1), lf.one, lf.oneBytes);
    }
    for (int k = 0; k < 2; ++k) {
        const std::vector<uint8_t>& f = k == 0 ? art.alphaDds : art.diffuseDds;
        gamefs::DdsImage img;
        if (!gamefs::parseDds(f.data(), f.size(), img).ok()) {
            r += " dds unparsable;";
            continue;
        }
        // the subresources' span (their offsets count from the file start when the image was not converted)
        size_t first = SIZE_MAX, end = 0;
        for (const gamefs::DdsSubresource& x : img.subresources) {
            first = std::min(first, x.offset);
            end = std::max(end, x.offset + x.size);
        }
        cmp(k == 0 ? 5u : 6u, img.pixels() + first, end - first);
    }
    const std::vector<uint8_t> zero(32 * 32 * 4, 0);
    cmp(7, zero.data(), zero.size());
    cmp(12, art.colorKey[0].data(), art.colorKey[0].size());
    for (const watershot::Stage::Cb& c : s.vs.cbs)
        if (c.slot == 0 && c.data.size() >= 64) {
            const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            r += std::string(" own World ") + (std::memcmp(c.data.data(), ident, 64) == 0 ? "equal (identity)" : "DIFFERS") + ";";
        }
    return r;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

struct Args {
    std::wstring mode, exportDir, exe, game, gt, tap, camera, out, system = L"ocean", own, legacy;
    bool module = false, warp = false;
    int view = 0, pass = -1;
    uint32_t reflect = 0;  // --reflect RRGGBB: a constant reflection colour in place of the zero texture (diagnostic)
    bool haveReflect = false;
};

int usage() {
    std::fprintf(stderr,
                 "usage: watershot replay EXPORT --exe EXE [--game DIR] [--system ocean|all] [--module] [--gt DIR | --tap FILE] [--out DIR] [--warp]\n"
                 "       watershot frame EXPORT --exe EXE --game DIR [--gt DIR | --tap FILE]\n"
                 "       watershot live --camera CAM.json (--gt DIR | --tap FILE) --exe EXE [--game DIR] [--view I] [--pass P] [--out PNG] [--warp]\n");
    return 2;
}

int fail(const Status& st, const char* what, int code) {
    std::fprintf(stderr, "watershot: %s: %s\n", what, st.str().c_str());
    return code;
}

bool loadGrid(const Args& a, heightfield::HeightGrid& g, Status& st) {
    if (!a.gt.empty()) st = terrainshot::loadGroundTruth(a.gt, g);
    else if (!a.tap.empty()) st = terrainshot::loadTapFile(a.tap, g);
    else return false;
    return true;
}

// One raw WaterFrame block (e.g. an export's buffers/<res>_<sha>.bin of an ocean draw's cb1) checked member by member.
int runFrameFile(const Args& a) {
    if (a.game.empty() || a.exe.empty()) return usage();
    std::vector<uint8_t> block;
    {
        FILE* f = _wfopen(a.exportDir.c_str(), L"rb");
        if (!f) return fail(Status::fail(Err::Io, "cannot open the block"), "framefile", 4);
        block.resize(0x1000);
        block.resize(fread(block.data(), 1, block.size(), f));
        fclose(f);
    }
    if (block.size() < water::kWaterFrameBytes) return fail(Status::fail(Err::Truncated, "block shorter than 0x250"), "framefile", 4);
    terrainshot::Device d;
    Status st = d.create(a.warp);
    if (!st.ok()) return fail(st, "device", 6);
    water::Renderer r;
    st = r.init(d.dev, a.exe, a.game);
    if (!st.ok()) return fail(st, "water module init", 6);
    water::World world;
    heightfield::HeightGrid g;
    Status gs;
    if (loadGrid(a, g, gs)) {
        if (!gs.ok()) return fail(gs, "height field", 4);
        world = water::worldFromGrid(g);
    }
    std::vector<watershot::MemberCheck> checks;
    water::FrameInputs used;
    st = watershot::checkWaterFrame(block.data(), r.stockFields(), world.valid ? &world : nullptr, checks, &used);
    if (!st.ok()) return fail(st, "frame check", 4);
    std::printf("world box (%.9g, %.9g)..(%.9g, %.9g), width %.9g, height %.9g, samples %u x %u%s\n", used.world.minX, used.world.minY,
                used.world.maxX, used.world.maxY, used.world.width, used.world.height, used.world.samplesX, used.world.samplesY,
                world.valid ? "" : " [solved from the block]");
    std::printf("solved: +0xDC %.9g, +0xE4 %.9g, +0xC8 %.9g, fog density %.9g scale %.9g, camera (%.9g, %.9g, %.9g)\n", used.fields.get(0xDC),
                used.fields.get(0xE4), used.fields.get(0xC8), used.fog.density, used.fog.scale, used.camPos[0], used.camPos[1], used.camPos[2]);
    int eq = 0, all = 0;
    for (const watershot::MemberCheck& c : checks) {
        ++all;
        eq += c.equal;
        std::printf("  %-22s 0x%03x %-6s %s", c.name.c_str(), c.offset, c.input ? "input" : "module", c.equal ? "equal" : "DIFFERS");
        if (!c.equal) std::printf(" (max relative difference %.3g)", c.maxRelDiff);
        std::printf("\n");
    }
    std::printf("%d of %d members bit-equal\n", eq, all);
    return 0;
}

int runFrameOrReplay(const Args& a) {
    watershot::ExportIndex idx;
    Status st = watershot::loadIndex(a.exportDir, idx);
    if (!st.ok()) return fail(st, "export index", 4);
    terrainshot::Device d;
    st = d.create(a.warp);
    if (!st.ok()) return fail(st, "device", 6);
    water::WaterFields stock;
    const bool needFields = a.mode == L"frame" || a.module;
    water::Renderer r;   // kept: its own inputs replace the capture's with --own (issue 3609)
    if (needFields) {
        if (a.game.empty() || a.exe.empty()) return usage();
        st = r.init(d.dev, a.exe, a.game);
        if (!st.ok()) return fail(st, "water module init", 6);
        stock = r.stockFields();
    }
    water::World world;
    {
        heightfield::HeightGrid g;
        Status gs;
        if (loadGrid(a, g, gs)) {
            if (!gs.ok()) return fail(gs, "height field", 4);
            world = water::worldFromGrid(g);
        }
    }
    std::printf("watershot %s: capture %s, %zu draws in the export, adapter %s\n", narrow(a.mode).c_str(), idx.capture.c_str(),
                idx.draws.size(), d.adapter.c_str());
    // the draws of the system, loaded
    // Draws the exporter left unclassified ("candidate") are classified by their vertex shader: the EXE's ocean VS
    // (0x06815A48, WaterShader passes 0/2/3/5) makes an ocean draw.
    std::vector<uint8_t> oceanVs;
    if (!a.exe.empty()) {
        shaders::ShaderSection sec;
        if (sec.open(a.exe).ok())
            if (const shaders::ShaderBlob* b = sec.findByVa(0x06815a48)) oceanVs.assign(b->bytes, b->bytes + b->size);
    }
    std::vector<watershot::DrawEntry> sel;
    size_t candidates = 0, classified = 0;
    for (watershot::DrawEntry e : idx.draws) {
        if (e.system == "candidate" && !oceanVs.empty()) {
            ++candidates;
            std::vector<uint8_t> code;
            if (watershot::drawVertexShader(a.exportDir, e, code).ok() && code == oceanVs) {
                e.system = "ocean";
                ++classified;
            }
        }
        const std::string want = narrow(a.system);
        if (a.system == L"all" || e.system == want || e.system.rfind(want + "_", 0) == 0) sel.push_back(e);  // "ocean" takes "ocean_p3"
    }
    if (candidates) std::printf("%zu candidate draws, %zu of them with the EXE's ocean VS (classified ocean)\n", candidates, classified);
    std::vector<watershot::CapturedDraw> states(sel.size());
    for (size_t i = 0; i < sel.size(); ++i) {
        st = watershot::loadDrawState(a.exportDir, sel[i], states[i]);
        if (!st.ok()) return fail(st, ("draw " + std::to_string(sel[i].n)).c_str(), 4);
    }
    // views: runs of equal WaterFrame blocks among the ocean draws
    std::vector<std::vector<uint8_t>> frames;
    std::vector<int> viewOf(sel.size(), -1);
    for (size_t i = 0; i < sel.size(); ++i) {
        if (!isOcean(sel[i].system) && !isRiver(sel[i].system)) continue;
        std::vector<uint8_t> f;
        if (!watershot::capturedWaterFrame(states[i], f).ok()) continue;
        if (frames.empty() || frames.back() != f) frames.push_back(f);
        viewOf[i] = static_cast<int>(frames.size()) - 1;
    }
    // module parts: per view the module's WaterFrame, the footprints, the mesh
    std::vector<std::vector<uint8_t>> mine(frames.size());
    water::OceanGeometry geo;
    water::RiverArt riverArt;
    bool haveRiverArt = false;
    if (a.module && !a.game.empty()) haveRiverArt = water::loadRiverArt(a.game, riverArt).ok();
    bool geoOk = false;
    if (needFields && !frames.empty()) {
        std::vector<water::Footprint> fps(frames.size());
        for (size_t v = 0; v < frames.size(); ++v) {
            std::vector<watershot::MemberCheck> checks;
            water::FrameInputs used;
            st = watershot::checkWaterFrame(frames[v].data(), stock, world.valid ? &world : nullptr, checks, &used);
            if (!st.ok()) return fail(st, "frame check", 4);
            if (!world.valid) world = used.world;
            mine[v].resize(water::kWaterFrameBytes);
            st = water::computeWaterFrame(used, mine[v].data());
            if (!st.ok()) return fail(st, "computeWaterFrame", 4);
            if (!a.camera.empty()) {
                // the scene's inputs (srv_water_scene.cpp): the live camera, the module's defaults for everything the
                // EXE's globals hold (fog, shadow blend, overlay and shadow matrices), the stock fields with the
                // block's clock (+0xDC, +0xE4: the phase is the capture's so that only the other inputs differ)
                water::FrameInputs sc;
                st = terrainshot::loadCamera(a.camera, static_cast<int>(v), sc.cam);
                if (!st.ok()) return fail(st, "camera", 4);
                sc.fields = stock;
                sc.fields.set(0xDC, used.fields.get(0xDC));
                sc.fields.set(0xE4, used.fields.get(0xE4));
                sc.world = world;
                float inv[16];
                {
                    // camera height: the inverse view's translation z (camWorld when the JSON has it)
                    const float* cw = sc.cam.camWorld;
                    float h = cw[14];
                    if (h == 0.0f) {
                        // fall back on the block's camera position
                        h = used.camPos[2];
                    }
                    (void)inv;
                    sc.fog.w = water::zoomFraction(h);
                    // --legacy fog / shadow: the scene's inputs before issue 3609 (no fog, grey fog colour, zoom 0;
                    // shadow blend 0)
                    const std::string lg = narrow(a.legacy);
                    if (lg.find("fog") != std::string::npos) {
                        sc.fog = water::Fog();
                        sc.fog.density = 0.0f;
                        sc.fog.scale = 1.0f;
                        sc.fog.w = 0.0f;
                        sc.fog.color[0] = sc.fog.color[1] = sc.fog.color[2] = 0.5f;
                    }
                    if (lg.find("shadow") != std::string::npos) sc.shadowBlend = 0.0f;
                }
                std::vector<uint8_t> scene(water::kWaterFrameBytes);
                st = water::computeWaterFrame(sc, scene.data());
                if (!st.ok()) return fail(st, "computeWaterFrame (scene inputs)", 4);
                std::printf("view %zu: the scene's WaterFrame against the capture's, member by member:\n", v);
                struct M { const char* n; uint32_t off, bytes; };
                const M ms[] = {{"ViewProj", 0x000, 64}, {"OverlayProj", 0x040, 64}, {"WaveScale", 0x080, 16}, {"WavePhase", 0x090, 16},
                                {"WaveConst", 0x0A0, 16}, {"WaveR11", 0x0B0, 4}, {"WaveR12", 0x0C0, 4}, {"R13", 0x0D0, 4},
                                {"+0x90", 0x0E0, 12}, {"+0x9C", 0x0F0, 12}, {"+0x70", 0x100, 16}, {"+0x80", 0x110, 16},
                                {"+0xA8", 0x120, 16}, {"+0xB8", 0x130, 16}, {"FogParams", 0x140, 16}, {"CameraPos", 0x150, 12},
                                {"ShadowMatrix1", 0x160, 64}, {"ShadowMatrix0", 0x1A0, 64}, {"HeightmapUV", 0x1E0, 64},
                                {"ShadowBlend", 0x220, 4}, {"FogColor", 0x230, 16}, {"+0x6C", 0x240, 4}};
                for (const M& m : ms) {
                    double worst = 0;
                    for (uint32_t k = 0; k < m.bytes; k += 4) {
                        float x, y;
                        std::memcpy(&x, scene.data() + m.off + k, 4);
                        std::memcpy(&y, frames[v].data() + m.off + k, 4);
                        if (x != y) worst = std::max(worst, std::fabs(static_cast<double>(x) - y) / std::max(1e-30, std::fabs(static_cast<double>(y))));
                    }
                    std::printf("  %-14s 0x%03x %s", m.n, m.off, worst == 0 ? "equal" : "DIFFERS");
                    if (worst != 0) std::printf(" (max relative %.3g)", worst);
                    std::printf("\n");
                }
                mine[v] = scene;
            }
            st = water::viewFootprint(reinterpret_cast<const float*>(mine[v].data()), world, fps[v]);
            if (!st.ok()) return fail(st, "footprint", 4);
            if (a.mode == L"frame") {
                std::printf("view %zu: world box (%g, %g)..(%g, %g), samples %u x %u%s\n", v, world.minX, world.minY, world.maxX, world.maxY,
                            world.samplesX, world.samplesY, a.gt.empty() && a.tap.empty() ? " [solved from the block]" : "");
                int eq = 0, all = 0;
                for (const watershot::MemberCheck& c : checks) {
                    ++all;
                    eq += c.equal;
                    std::printf("  %-22s 0x%03x %-6s %s", c.name.c_str(), c.offset, c.input ? "input" : "module", c.equal ? "equal" : "DIFFERS");
                    if (!c.equal) std::printf(" (max relative difference %.3g)", c.maxRelDiff);
                    std::printf("\n");
                }
                std::printf("  %d of %d members bit-equal\n", eq, all);
            }
        }
        if (a.mode == L"frame") return 0;
        st = water::buildOceanGeometry(world, fps.data(), static_cast<uint32_t>(fps.size()), geo);
        geoOk = st.ok();
        if (!geoOk) std::printf("module mesh: %s\n", st.str().c_str());
    } else if (a.mode == L"frame") {
        std::printf("no ocean draw with a WaterFrame in the export\n");
        return 0;
    }
    // --legacy lean / currents / shadow: the module's constant inputs before issue 3609, through setTexture
    if (!a.legacy.empty()) {
        const std::string lg = narrow(a.legacy);
        auto tex = [&](DXGI_FORMAT f, const void* px, uint32_t pitch) {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = td.Height = 1;
            td.MipLevels = td.ArraySize = 1;
            td.Format = f;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd = {px, pitch, 0};
            ID3D11Texture2D* t = nullptr;
            ID3D11ShaderResourceView* v = nullptr;
            if (SUCCEEDED(d.dev->CreateTexture2D(&td, &sd, &t))) d.dev->CreateShaderResourceView(t, nullptr, &v);
            if (t) t->Release();
            return v;
        };
        const uint16_t lean[4] = {0, 0, 0x8000, 0x8000};
        const uint16_t lean1 = 0x8000;
        const uint32_t neutral = 0x80808080u, white = 0xFFFFFFFFu;
        auto set = [&](water::Slot sl, ID3D11ShaderResourceView* v) {
            r.setTexture(sl, v);
            if (v) v->Release();
        };
        if (lg.find("lean") != std::string::npos) {
            set(water::Slot::Lean0, tex(DXGI_FORMAT_R16G16B16A16_UNORM, lean, 8));
            set(water::Slot::Lean1, tex(DXGI_FORMAT_R16_UNORM, &lean1, 2));
        }
        if (lg.find("currents") != std::string::npos) set(water::Slot::Currents, tex(DXGI_FORMAT_R8G8B8A8_UNORM, &neutral, 4));
        if (lg.find("shadow") != std::string::npos) set(water::Slot::Shadow, tex(DXGI_FORMAT_R8G8B8A8_UNORM, &white, 4));
        std::printf("legacy inputs: %s\n", lg.c_str());
    }
    // --own needs the module's currents map: a field of the world's size (the map depends only on the grid's plots and
    // a null t0, 3602) and one draw, which computes it
    if (!a.own.empty() && world.valid && world.samplesX % 64 == 0 && world.samplesY % 64 == 0) {
        heightfield::HeightGrid g;
        g.mapW = world.samplesX / 64;
        g.mapH = world.samplesY / 64;
        g.samplesX = world.samplesX;
        g.samplesY = world.samplesY;
        g.wrapX = world.wrapX;
        g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 0);
        st = r.setHeightField(g);
        if (!st.ok()) return fail(st, "module height field (for the currents map)", 6);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 64;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* tt = nullptr;
        ID3D11RenderTargetView* tv = nullptr;
        if (SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &tt)) && SUCCEEDED(d.dev->CreateRenderTargetView(tt, nullptr, &tv))) {
            water::FrameInputs fi;
            const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -500, 1};
            const float proj[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1.0002f, -1, 0, 0, -1.0002f, 0};
            std::memcpy(fi.cam.view, view, sizeof view);
            std::memcpy(fi.cam.proj, proj, sizeof proj);
            fi.cam.width = fi.cam.height = 64;
            fi.fields = stock;
            water::FrameStats fs;
            st = r.draw(d.ctx, tv, nullptr, fi, water::Options(), &fs);
            if (!st.ok()) std::printf("note: the module's priming draw failed: %s\n", st.str().c_str());
        }
        if (tv) tv->Release();
        if (tt) tt->Release();
    }
    // replay
    uint64_t total = 0, equal = 0;
    size_t oceanIdx = 0;
    for (size_t i = 0; i < sel.size(); ++i) {
        watershot::ModuleParts mp;
        const watershot::ModuleParts* use = nullptr;
        std::string meshNote;
        if (a.module && isOcean(sel[i].system) && viewOf[i] >= 0) {
            mp.frame = mine[static_cast<size_t>(viewOf[i])].data();
            if (geoOk && oceanIdx < geo.batches.size()) {
                const water::OceanBatch& b = geo.batches[oceanIdx];
                mp.vertices = &geo.vertices;
                mp.indices = &geo.indices;
                mp.startIndex = b.firstQuad * 6;
                mp.indexCount = b.quads * 6;
                // the mesh check: the capture's quads of this draw against the module's batch
                const watershot::CapturedDraw& s = states[i];
                const bool sameCount = s.indexCount == b.quads * 6;
                uint32_t eqQuads = 0;
                if (sameCount && !s.vbs.empty() && s.ibFormat == DXGI_FORMAT_R16_UINT) {
                    for (uint32_t q = 0; q < b.quads; ++q) {
                        uint16_t first = 0;
                        std::memcpy(&first, s.ib.data() + s.ibOffset + (static_cast<size_t>(s.startIndex) + q * 6) * 2, 2);
                        const size_t off = static_cast<size_t>(first) * 16;
                        if (off + 64 <= s.vbs[0].data.size() &&
                            std::memcmp(s.vbs[0].data.data() + off, geo.vertices.data() + static_cast<size_t>(b.firstQuad + q) * 16, 64) == 0)
                            ++eqQuads;
                    }
                }
                meshNote = "mesh: batch " + std::to_string(oceanIdx) + " (view " + std::to_string(b.view) + ", page " + std::to_string(b.page) +
                           ") " + std::to_string(b.quads) + " quads, capture " + std::to_string(s.indexCount / 6) + "; quads bit-equal " +
                           std::to_string(eqQuads);
            } else {
                meshNote = "mesh: no module batch for this draw (the capture has more ocean draws than the module)";
            }
            use = &mp;
        } else if (a.module && isRiver(sel[i].system) && viewOf[i] >= 0) {
            // rivers (3605): the module's WaterFrame (the river binds view 0's block) in place of the capture's; the
            // mesh is the tapped one, so the capture's buffers stay; the module's river inputs compared byte for byte
            mp.frame = mine[static_cast<size_t>(viewOf[i])].data();
            meshNote = riverInputs(states[i], riverArt, haveRiverArt);
            use = &mp;
        }
        if (isOcean(sel[i].system)) ++oceanIdx;
        watershot::ReplayResult res;
        res.draw = sel[i];
        if (use && !a.own.empty()) {
            // the module's own inputs in place of the capture's at the listed PS slots ("all": 0 1 4 5 6 8 9 10 11 13)
            std::string list = narrow(a.own);
            if (list == "all") list = "0,1,4,5,6,8,9,10,11,13";
            size_t pos = 0;
            while (pos <= list.size()) {
                const size_t c = list.find(',', pos);
                const std::string tok = list.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
                const int slot = std::atoi(tok.c_str());
                if (!tok.empty() && slot >= 0 && slot < 14 && isOcean(sel[i].system))
                    mp.psSrv[slot] = r.inputTexture(static_cast<water::Slot>(slot));
                if (c == std::string::npos) break;
                pos = c + 1;
            }
        }
        st = watershot::replayDraw(d.dev, d.ctx, states[i], use, res);
        if (!st.ok()) {
            std::printf("draw %u (%s): replay failed: %s\n", sel[i].n, sel[i].system.c_str(), st.str().c_str());
            continue;
        }
        std::printf("draw %u (%s)%s: %s\n", sel[i].n, sel[i].system.c_str(), use ? " [module]" : "",
                    res.compared ? watershot::describe(res.cmp).c_str() : "not compared");
        if (!meshNote.empty()) std::printf("  %s\n", meshNote.c_str());
        if (use) {
            // the module's parts against the capture's own parts, same textures, same states, same start: what the
            // module changes in pixels, whether or not the export has the EXE's target
            watershot::ReplayResult own;
            if (watershot::replayDraw(d.dev, d.ctx, states[i], nullptr, own).ok() && own.rgba.size() == res.rgba.size() && !own.rgba.empty()) {
                watershot::Compare c;
                if (watershot::compareRgba8(res.rgba.data(), res.width * 4, own.rgba.data(), own.width * 4, res.width, res.height, c).ok())
                    std::printf("  module parts against the capture's parts: %s\n", watershot::describe(c).c_str());
            }
        }
        for (const std::string& n : res.notes) std::printf("  note: %s\n", n.c_str());
        if (res.compared) {
            total += res.cmp.total;
            equal += res.cmp.equal;
        }
        if (!a.out.empty()) {
            CreateDirectoryW(a.out.c_str(), nullptr);
            std::vector<uint8_t> rgba = res.rgba;
            if (states[i].rtFormat == DXGI_FORMAT_B8G8R8A8_UNORM || states[i].rtFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                for (size_t k = 0; k < rgba.size(); k += 4) std::swap(rgba[k], rgba[k + 2]);
            const std::wstring png = a.out + L"/draw_" + std::to_wstring(sel[i].n) + (use ? L"_module" : L"_replay") + L".png";
            terrainshot::writePngRgba(png, res.width, res.height, rgba);
        }
    }
    if (total)
        std::printf("all compared draws: %llu of %llu pixels equal (%.4f %%)\n", static_cast<unsigned long long>(equal),
                    static_cast<unsigned long long>(total), 100.0 * static_cast<double>(equal) / static_cast<double>(total));
    return 0;
}

int runLive(const Args& a) {
    if (a.camera.empty() || a.exe.empty() || (a.gt.empty() && a.tap.empty())) return usage();
    heightfield::HeightGrid g;
    Status st;
    loadGrid(a, g, st);
    if (!st.ok()) return fail(st, "height field", 4);
    water::Camera cam;
    st = terrainshot::loadCamera(a.camera, a.view, cam);
    if (!st.ok()) return fail(st, "camera", 4);
    terrainshot::Device d;
    st = d.create(a.warp);
    if (!st.ok()) return fail(st, "device", 6);
    water::Renderer r;
    st = r.init(d.dev, a.exe, a.game);
    if (!st.ok()) return fail(st, "water module init", 6);
    st = r.setHeightField(g);
    if (!st.ok()) return fail(st, "height field", 4);
    if (a.haveReflect) {
        const uint32_t px = 0xFF000000u | ((a.reflect & 0xFF) << 16) | (a.reflect & 0xFF00) | ((a.reflect >> 16) & 0xFF);
        D3D11_TEXTURE2D_DESC rd = {};
        rd.Width = rd.Height = 1;
        rd.MipLevels = rd.ArraySize = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Usage = D3D11_USAGE_IMMUTABLE;
        rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {&px, 4, 0};
        ID3D11Texture2D* rt = nullptr;
        ID3D11ShaderResourceView* rv = nullptr;
        if (SUCCEEDED(d.dev->CreateTexture2D(&rd, &sd, &rt)) && SUCCEEDED(d.dev->CreateShaderResourceView(rt, nullptr, &rv))) r.setTexture(water::Slot::Reflection, rv);
        if (rv) rv->Release();
        if (rt) rt->Release();
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = cam.width;
    td.Height = cam.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* stg = nullptr;
    if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &tex)) || FAILED(d.dev->CreateRenderTargetView(tex, nullptr, &rtv)))
        return fail(Status::fail(Err::Device, "target"), "target", 6);
    const float clear[4] = {84 / 255.0f, 84 / 255.0f, 84 / 255.0f, 1};
    d.ctx->ClearRenderTargetView(rtv, clear);
    water::FrameInputs in;
    in.cam = cam;
    in.fields = r.stockFields();
    water::Options o;
    o.oceanPass = a.pass;
    water::FrameStats fs;
    st = r.draw(d.ctx, rtv, nullptr, in, o, &fs);
    if (!st.ok()) return fail(st, "draw", 4);
    double gpu = -1;
    r.gpuTime(d.ctx, true, &gpu);
    std::printf("watershot live: %u x %u, pass %d, %u ocean draws, %u quads, pages missing %u, GPU %.3f ms (indication, COMMON 23), adapter %s\n",
                cam.width, cam.height, fs.oceanPassUsed, fs.oceanDraws, fs.oceanQuads, fs.pagesMissing, gpu, d.adapter.c_str());
    if (!a.out.empty()) {
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(d.dev->CreateTexture2D(&td, nullptr, &stg))) return fail(Status::fail(Err::Device, "staging"), "staging", 6);
        d.ctx->CopyResource(stg, tex);
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(d.ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) return fail(Status::fail(Err::Device, "map"), "map", 6);
        std::vector<uint8_t> rgba(static_cast<size_t>(cam.width) * cam.height * 4);
        for (uint32_t y = 0; y < cam.height; ++y)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * cam.width * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                        cam.width * 4);
        d.ctx->Unmap(stg, 0);
        st = terrainshot::writePngRgba(a.out, cam.width, cam.height, rgba);
        if (!st.ok()) return fail(st, "png", 4);
        std::printf("wrote %s\n", narrow(a.out).c_str());
    }
    if (stg) stg->Release();
    rtv->Release();
    tex->Release();
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return usage();
    Args a;
    a.mode = argv[1];
    int i = 2;
    if (a.mode == L"replay" || a.mode == L"frame" || a.mode == L"framefile") {
        if (argc < 3) return usage();
        a.exportDir = argv[2];
        i = 3;
    } else if (a.mode != L"live") {
        return usage();
    }
    for (; i < argc; ++i) {
        const std::wstring k = argv[i];
        auto val = [&](std::wstring& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        std::wstring n;
        if (k == L"--exe") { if (!val(a.exe)) return usage(); }
        else if (k == L"--game") { if (!val(a.game)) return usage(); }
        else if (k == L"--gt") { if (!val(a.gt)) return usage(); }
        else if (k == L"--tap") { if (!val(a.tap)) return usage(); }
        else if (k == L"--camera") { if (!val(a.camera)) return usage(); }
        else if (k == L"--out") { if (!val(a.out)) return usage(); }
        else if (k == L"--system") { if (!val(a.system)) return usage(); }
        else if (k == L"--view") { if (!val(n)) return usage(); a.view = _wtoi(n.c_str()); }
        else if (k == L"--pass") { if (!val(n)) return usage(); a.pass = _wtoi(n.c_str()); }
        else if (k == L"--reflect") { if (!val(n)) return usage(); a.reflect = static_cast<uint32_t>(wcstoul(n.c_str(), nullptr, 16)); a.haveReflect = true; }
        else if (k == L"--module") a.module = true;
        else if (k == L"--warp") a.warp = true;
        else if (k == L"--own") { if (!val(a.own)) return usage(); }
        else if (k == L"--legacy") { if (!val(a.legacy)) return usage(); }
        else return usage();
    }
    if (a.mode == L"live") return runLive(a);
    if (a.mode == L"framefile") return runFrameFile(a);
    return runFrameOrReplay(a);
}
