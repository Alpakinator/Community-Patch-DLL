// test_draw.cpp - the unit renderer end to end on WARP (issue 4352): open from the install and the EXE, prepare a
// scene with fixed pose and placement sources, check the frame plan (statuses per unit, blocks, palette header rows,
// the id stream, material 87 / 89 / 90 draws), draw the three passes into WARP targets, and every status reached
// through prepare (BadInput, NoArtRow, NoMembers, ModelNotFound, PlacementFailed, ClipNotFound, CurveUnsupported).
#include <d3d11.h>
#include <cmath>
#include <cstring>

#include "units_fixture.h"

using namespace c5r::units;

namespace {

// Pose: every bone [I | T] (rigid figure at its placement); clip "nope" -> ClipNotFound, "bent" -> CurveUnsupported.
class FixedPose : public PoseSource {
public:
    int calls = 0;
    UnitStatus rows(const PoseRequest& r, Float4 out[kPaletteBones * 3]) override {
        ++calls;
        if (r.clip == "nope") return UnitStatus::fail(UnitCode::ClipNotFound, "clip nope");
        if (r.clip == "bent") return UnitStatus::fail(UnitCode::CurveUnsupported, "format 13");
        for (uint32_t k = 0; k < kPaletteBones; ++k) {
            out[3 * k + 0] = {1, 0, 0, r.T[0]};
            out[3 * k + 1] = {0, 1, 0, r.T[1]};
            out[3 * k + 2] = {0, 0, 1, r.T[2]};
        }
        return UnitStatus::Ok();
    }
};

// Placement: every figure alive, 12 units apart along x from the plot (x * 100, y * 100); seed 99 fails.
class FixedPlacement : public PlacementSource {
public:
    UnitStatus place(const PlacementRequest& r, UnitPlacement& out) override {
        out = UnitPlacement();
        if (r.unit->formation_seed == 99) return UnitStatus::fail(UnitCode::PlacementFailed, "seed 99");
        out.figures = out.living = r.art->figures();
        for (uint32_t f = 0; f < out.figures; ++f) {
            FigurePlacement p;
            p.figure = static_cast<uint16_t>(f);
            p.slot = p.figure;
            p.x = r.unit->plot_x * 100.0f + 12.0f * f - 60.0f;
            p.y = r.unit->plot_y * 100.0f;
            out.placed.push_back(p);
        }
        return UnitStatus::Ok();
    }
};

UnitInput unit(int32_t id, const char* type, int32_t x, int32_t y) {
    UnitInput u;
    u.player = 0;
    u.id = id;
    u.unit_type = type;
    u.plot_x = x;
    u.plot_y = y;
    return u;
}

UnitScene scene() {
    UnitScene s;
    s.water_level = 10.117f;
    PlayerInput p;
    p.player = 0;
    p.colour = "PLAYERCOLOR_POLYNESIA";
    p.art_style = "CIVILIZATION_POLYNESIA";
    p.era = "ERA_ANCIENT";
    s.players.push_back(p);
    s.units.push_back(unit(1, "UNIT_WARRIOR", 0, 0));    // 0: Ok, 12 figures x 1 mesh
    s.units.push_back(unit(2, "UNIT_TRIREME", 1, 0));    // 1: Ok, 1 figure x meshes
    UnitInput u = unit(3, "UNIT_WARRIOR", 0, 1);
    u.player = 5;                                         // 2: BadInput (no such player)
    s.units.push_back(u);
    s.units.push_back(unit(4, "UNIT_NOPE", 0, 0));       // 3: NoArtRow
    s.units.push_back(unit(5, "UNIT_NOMEMBERS", 0, 0));  // 4: NoMembers
    s.units.push_back(unit(6, "UNIT_BADMODEL", 0, 0));   // 5: ModelNotFound
    u = unit(7, "UNIT_WARRIOR", 0, 0);
    u.formation_seed = 99;                                // 6: PlacementFailed
    s.units.push_back(u);
    u = unit(8, "UNIT_WARRIOR", 0, 0);
    FigureInput f;
    f.figure = 4;
    f.clip = "nope";
    u.figures.push_back(f);                               // 7: ClipNotFound
    s.units.push_back(u);
    u.id = 9;
    u.figures[0].clip = "bent";                           // 8: CurveUnsupported
    s.units.push_back(u);
    u.id = 10;
    u.figures[0].figure = 12;                             // 9: BadInput (figure past the 12)
    s.units.push_back(u);
    u = unit(11, "UNIT_NOART", 0, 0);                     // 10: NoArtRow (UnitArtInfo names no row)
    s.units.push_back(u);
    return s;
}

ID3D11Device* warp(ID3D11DeviceContext** ctx) {
    ID3D11Device* dev = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                   nullptr, ctx);
    return SUCCEEDED(hr) ? dev : nullptr;
}

C5R_TEST(prepare_statuses_and_plan) {
    units_test::InstallFixture fx;
    UnitRenderer r;
    CHECK_OK(r.open(nullptr, fx.gf, fx.db, fx.exe));
    FixedPose pose;
    FixedPlacement place;
    r.setPoseSource(&pose);
    r.setPlacementSource(&place);
    FramePlan plan;
    CHECK_OK(r.prepare(scene(), nullptr, plan));
    CHECK_EQ(plan.units.size(), static_cast<size_t>(11));
    const UnitCode want[] = {UnitCode::Ok, UnitCode::Ok, UnitCode::BadInput, UnitCode::NoArtRow, UnitCode::NoMembers,
                             UnitCode::ModelNotFound, UnitCode::PlacementFailed, UnitCode::ClipNotFound,
                             UnitCode::CurveUnsupported, UnitCode::BadInput, UnitCode::NoArtRow};
    for (size_t i = 0; i < plan.units.size() && i < 11; ++i) {
        CHECK_UNIT(plan.units[i].status, want[i]);
        if (!plan.units[i].status.ok()) CHECK(plan.units[i].draws.empty());
    }
    CHECK_EQ(plan.drawn(), static_cast<size_t>(2));
    const UnitPlan& w = plan.units[0];
    CHECK_EQ(w.first_block, 0u);
    CHECK_EQ(w.blocks, 12u);                       // 3 + 6 + 3 figures, one skinned mesh each
    CHECK_EQ(w.draws.size(), static_cast<size_t>(3));
    if (w.draws.size() == 3) {
        CHECK_EQ(w.draws[0].instances(), 3u);
        CHECK_EQ(w.draws[1].instances(), 6u);
        CHECK_EQ(w.draws[2].instances(), 3u);
        CHECK_EQ(w.draws[1].blocks[0], 3u);        // figures in member order, blocks consecutive
    }
    const UnitPlan& t = plan.units[1];
    CHECK_EQ(t.first_block, 12u);
    CHECK(t.blocks >= 1u);
    CHECK_EQ(plan.blocks, w.blocks + t.blocks);
    CHECK_EQ(plan.palette.size(), static_cast<size_t>(plan.blocks) * kPaletteRows);
    // header rows of every block: team colour, (1, 0, 0) fade
    for (uint32_t b = 0; b < plan.blocks; ++b) {
        const Float4& c = plan.palette[static_cast<size_t>(b) * kPaletteRows];
        const Float4& f = plan.palette[static_cast<size_t>(b) * kPaletteRows + 1];
        CHECK_EQ(c.x, 217.0f * (1.0f / 255.0f));   // byte x float32(1/255) (4453, unit-draws.md 5.1)
        CHECK_EQ(c.y, 88.0f * (1.0f / 255.0f));
        CHECK_EQ(c.w, 1.0f);
        CHECK_EQ(f.x, 1.0f);
        CHECK_EQ(f.y, 0.0f);
        CHECK_EQ(f.z, 0.0f);
    }
    // the bone rows carry the figure's translation
    CHECK_EQ(plan.palette[2].w, -60.0f);
    CHECK_EQ(plan.palette[kPaletteRows + 2].w, -48.0f);
    // id stream: three passes, each with every instanced draw
    IdStream ids;
    buildIdStream(plan, 3, ids);
    uint32_t perPass = 0, draws = 0;
    for (auto& u : plan.units)
        for (auto& d : u.draws)
            if (d.material != Material::Static) {
                perPass += static_cast<uint32_t>(d.blocks.size());
                ++draws;
            }
    CHECK_EQ(ids.ids.size(), static_cast<size_t>(perPass) * 3);
    CHECK_EQ(ids.offsets.size(), static_cast<size_t>(3));
    CHECK_EQ(ids.offsets[1].size(), static_cast<size_t>(draws));
    CHECK_EQ(ids.offsets[1][0], perPass * 4);
    CHECK_EQ(ids.ids[3], 3u);
}

// Issue 4464: the art cache per art row. Over many frames the cached renderer's plans equal the uncached rule's (every
// status, block, palette row and draw), each art row's members' .fxsxml is parsed once (the uncached rule parses them
// for every unit of every frame), and open() drops the cache.
C5R_TEST(art_cache_per_row) {
    units_test::InstallFixture fx;
    UnitRenderer on, off;
    CHECK_OK(on.open(nullptr, fx.gf, fx.db, fx.exe));
    CHECK_OK(off.open(nullptr, fx.gf, fx.db, fx.exe));
    CHECK(on.artCache());
    off.setArtCache(false);
    FixedPose pose;
    FixedPlacement place;
    for (UnitRenderer* r : {&on, &off}) {
        r->setPoseSource(&pose);
        r->setPlacementSource(&place);
    }
    const UnitScene sc = scene();
    uint64_t onFirst = 0, offFirst = 0;
    const UnitModel* warriorModel = nullptr;
    const int kFrames = 30;
    for (int frame = 1; frame <= kFrames; ++frame) {
        FramePlan a, b;
        CHECK_OK(on.prepare(sc, nullptr, a));
        CHECK_OK(off.prepare(sc, nullptr, b));
        CHECK_EQ(a.units.size(), b.units.size());
        CHECK_EQ(a.blocks, b.blocks);
        CHECK_EQ(a.drawn(), static_cast<size_t>(2));
        CHECK_EQ(a.drawn(), b.drawn());
        CHECK_EQ(a.palette.size(), b.palette.size());
        CHECK(a.palette.size() == b.palette.size() &&
              std::memcmp(a.palette.data(), b.palette.data(), a.palette.size() * sizeof(Float4)) == 0);
        for (size_t i = 0; i < a.units.size() && i < b.units.size(); ++i) {
            const UnitPlan &x = a.units[i], &y = b.units[i];
            CHECK(x.status.code == y.status.code);
            CHECK_EQ(x.status.str(), y.status.str());
            CHECK(x.status.warnings == y.status.warnings);
            CHECK_EQ(static_cast<bool>(x.art), static_cast<bool>(y.art));
            if (x.art && y.art) {
                const std::string d = units_test::artDiff(*x.art, *y.art);
                if (!d.empty()) c5r::test::fail(__FILE__, __LINE__, "unit " + std::to_string(i) + ": " + d);
                CHECK(x.art->unit_type.empty());   // the row's art (units.h)
            }
            CHECK_EQ(x.first_block, y.first_block);
            CHECK_EQ(x.blocks, y.blocks);
            CHECK_EQ(x.draws.size(), y.draws.size());
            for (size_t k = 0; k < x.draws.size() && k < y.draws.size(); ++k) {
                CHECK(x.draws[k].material == y.draws[k].material);
                CHECK_EQ(x.draws[k].member, y.draws[k].member);
                CHECK(x.draws[k].blocks == y.draws[k].blocks);
                CHECK(x.draws[k].cb0.size() == y.draws[k].cb0.size());
                CHECK_EQ(x.draws[k].mesh->name, y.draws[k].mesh->name);
                CHECK_EQ(x.draws[k].model->gr2, y.draws[k].model->gr2);
            }
        }
        if (frame == 1) {
            onFirst = on.art().fxsxml_parses();
            offFirst = off.art().fxsxml_parses();
            warriorModel = a.units[0].draws.empty() ? nullptr : a.units[0].draws[0].model;
        }
        CHECK(!a.units[0].draws.empty() && a.units[0].draws[0].model == warriorModel);   // the same model every frame
    }
    // the warrior's 3 members, the trireme's 1 and the bad model's 1 (a failure, cached too); the unknown types and
    // the player-less unit reach no row
    CHECK_EQ(onFirst, static_cast<uint64_t>(5));
    CHECK_EQ(on.art().fxsxml_parses(), onFirst);                     // no parse after the first frame
    CHECK_EQ(on.cachedRows(), static_cast<size_t>(4));              // warrior, trireme, no members, bad model
    CHECK(offFirst > onFirst);                                      // five warriors of player 0 parse 3 each
    CHECK_EQ(off.art().fxsxml_parses(), offFirst * kFrames);         // the rule: every unit, every frame
    CHECK_EQ(off.cachedRows(), static_cast<size_t>(0));
    // loadUnit (the pre-check's call) through the cache equals the rule, the unit's type set
    UnitArt ca, ua;
    std::vector<const UnitModel*> cm, um;
    const UnitStatus cs = on.loadUnit(sc.units[0], &sc.players[0], ca, cm);
    const UnitStatus us = off.loadUnit(sc.units[0], &sc.players[0], ua, um);
    CHECK_UNIT(cs, UnitCode::Ok);
    CHECK_EQ(cs.str(), us.str());
    CHECK(units_test::artDiff(ca, ua).empty());
    CHECK_EQ(ca.unit_type, std::string("UNIT_WARRIOR"));
    CHECK_EQ(cm.size(), static_cast<size_t>(3));
    CHECK_EQ(cm.size(), um.size());
    CHECK_UNIT(on.loadUnit(sc.units[5], &sc.players[0], ca, cm), UnitCode::ModelNotFound);
    CHECK_EQ(ca.unit_type, std::string("UNIT_BADMODEL"));
    CHECK_UNIT(on.loadUnit(sc.units[3], &sc.players[0], ca, cm), UnitCode::NoArtRow);
    CHECK_EQ(on.art().fxsxml_parses(), onFirst);
    // a new open (a database reload): the cache is dropped and filled again
    CHECK_OK(on.open(nullptr, fx.gf, fx.db, fx.exe));
    CHECK_EQ(on.cachedRows(), static_cast<size_t>(0));
    CHECK_EQ(on.art().fxsxml_parses(), static_cast<uint64_t>(0));
    FramePlan c;
    CHECK_OK(on.prepare(sc, nullptr, c));
    CHECK_EQ(on.art().fxsxml_parses(), onFirst);
    CHECK_EQ(c.drawn(), static_cast<size_t>(2));
}

C5R_TEST(module_steps_without_sources) {
    units_test::InstallFixture fx;
    UnitRenderer r;
    CHECK_OK(r.open(nullptr, fx.gf, fx.db, fx.exe));
    UnitScene s = scene();
    s.units.resize(1);
    FramePlan plan;
    CHECK_OK(r.prepare(s, nullptr, plan));
    // the module's placement is a stub until 4354's placement.cpp is in the build
    CHECK_UNIT(plan.units[0].status, UnitCode::PlacementFailed);
    UnitRenderer closed;
    CHECK_ERR(closed.prepare(s, nullptr, plan), c5r::Err::NotFound);
}

C5R_TEST(draw_three_passes_on_warp) {
    units_test::InstallFixture fx;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Device* dev = warp(&ctx);
    if (!dev) c5r::test::skip("no WARP device");
    UnitRenderer r;
    CHECK_OK(r.open(dev, fx.gf, fx.db, fx.exe));
    FixedPose pose;
    FixedPlacement place;
    r.setPoseSource(&pose);
    r.setPlacementSource(&place);
    FramePlan plan;
    CHECK_OK(r.prepare(scene(), nullptr, plan));
    const UINT W = 256, H = 256;
    auto target = [&](DXGI_FORMAT fmt, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = W;
        d.Height = H;
        d.MipLevels = d.ArraySize = 1;
        d.Format = fmt;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        CHECK(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, tex)));
        CHECK(SUCCEEDED(dev->CreateRenderTargetView(*tex, nullptr, rtv)));
        const float clear[4] = {0, 0, 0, 0};
        ctx->ClearRenderTargetView(*rtv, clear);
    };
    ID3D11Texture2D *tScene = nullptr, *tMask = nullptr, *tDepth = nullptr;
    ID3D11RenderTargetView *vScene = nullptr, *vMask = nullptr;
    target(DXGI_FORMAT_R8G8B8A8_UNORM, &tScene, &vScene);
    target(DXGI_FORMAT_R16G16B16A16_FLOAT, &tMask, &vMask);
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = W;
    dd.Height = H;
    dd.MipLevels = dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHECK(SUCCEEDED(dev->CreateTexture2D(&dd, nullptr, &tDepth)));
    ID3D11DepthStencilView* dsv = nullptr;
    CHECK(SUCCEEDED(dev->CreateDepthStencilView(tDepth, nullptr, &dsv)));
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    // top-down orthographic camera over (-200..200, -200..200): clip = (x/200, y/200, 0.5 - z/1000, 1)
    PassCamera cam;
    cam.eye[0] = 0;
    cam.eye[1] = 0;
    cam.eye[2] = 500;
    cam.viewProj[0] = {1.0f / 200, 0, 0, 0};
    cam.viewProj[1] = {0, 1.0f / 200, 0, 0};
    cam.viewProj[2] = {0, 0, -1.0f / 1000, 0};
    cam.viewProj[3] = {0, 0, 0.5f, 1};
    cam.threshold = -1000;   // no unit below the water
    cam.fogColour = {0.5f, 0.7f, 0.9f, 1};
    PassSet ps;
    PassDesc p;
    p.viewport = {0, 0, static_cast<float>(W), static_cast<float>(H), 0, 1};
    p.cb1 = makeCb1(cam);
    p.kind = PassKind::ShadowMask;
    p.rtv = vMask;
    ps.passes.push_back(p);
    p.kind = PassKind::Scene;
    p.rtv = vScene;
    p.dsv = dsv;
    ps.passes.push_back(p);
    CHECK_OK(r.draw(ctx, plan, ps));
    // read both targets back
    auto covered = [&](ID3D11Texture2D* tex, DXGI_FORMAT fmt, uint32_t bpp) {
        D3D11_TEXTURE2D_DESC d = {};
        tex->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        (void)fmt;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &st))) return 0u;
        ctx->CopyResource(st, tex);
        D3D11_MAPPED_SUBRESOURCE m;
        uint32_t n = 0;
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            for (UINT y = 0; y < H; ++y) {
                const uint8_t* row = static_cast<const uint8_t*>(m.pData) + y * m.RowPitch;
                for (UINT x = 0; x < W; ++x) {
                    bool any = false;
                    for (uint32_t b = 0; b < bpp; ++b) any = any || row[x * bpp + b] != 0;
                    n += any ? 1 : 0;
                }
            }
            ctx->Unmap(st, 0);
        }
        st->Release();
        return n;
    };
    const uint32_t sceneCover = covered(tScene, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
    const uint32_t maskCover = covered(tMask, DXGI_FORMAT_R16G16B16A16_FLOAT, 8);
    std::printf("WARP: scene pixels covered %u, mask pixels covered %u\n", sceneCover, maskCover);
    CHECK(sceneCover > 100u);
    CHECK(maskCover > 100u);
    UnitRenderer cpuOnly;
    CHECK_OK(cpuOnly.open(nullptr, fx.gf, fx.db, fx.exe));
    CHECK_ERR(cpuOnly.draw(ctx, plan, ps), c5r::Err::Device);
    dsv->Release();
    tDepth->Release();
    vScene->Release();
    vMask->Release();
    tScene->Release();
    tMask->Release();
    ctx->Release();
    dev->Release();
}

C5R_TEST(open_refusals) {
    units_test::InstallFixture fx;
    UnitRenderer r;
    c5m::TsvTables empty;
    CHECK_ERR(r.open(nullptr, fx.gf, empty, fx.exe), c5r::Err::BadLayout);
    CHECK(!r.isOpen());
    // no EXE: open succeeds, every unit is ProgramNotFound
    UnitRenderer noExe;
    CHECK_OK(noExe.open(nullptr, fx.gf, fx.db, L"C:/no/such/CivilizationV_DX11.exe",
                        c5r::gamefs::fromUtf8(c5r::test::gameDir())));
    FixedPose pose;
    FixedPlacement place;
    noExe.setPoseSource(&pose);
    noExe.setPlacementSource(&place);
    UnitScene s = scene();
    s.units.resize(2);
    FramePlan plan;
    CHECK_OK(noExe.prepare(s, nullptr, plan));
    CHECK_UNIT(plan.units[0].status, UnitCode::ProgramNotFound);
    CHECK_UNIT(plan.units[1].status, UnitCode::ProgramNotFound);
    CHECK_EQ(plan.blocks, 0u);
}

C5R_TEST(programs_missing_one) {
    UnitPrograms p;
    UnitPrograms full;
    CHECK_OK(full.open(c5r::gamefs::fromUtf8(c5r::test::exePath())));
    std::vector<std::vector<uint8_t>> blobs;
    for (auto& r : unitProgramTable())
        if (!(r.material == Material::Static && !r.vertex && r.pass == PassKind::ShadowMask))
            blobs.push_back(*full.bytes(r.material, r.pass, r.vertex));
    CHECK_ERR(p.openFromBlobs(blobs), c5r::Err::NotFound);
    CHECK(p.bytes(Material::Skinned, PassKind::Scene, true) != nullptr);   // the others are kept
    CHECK(p.bytes(Material::Static, PassKind::ShadowMask, false) == nullptr);
}

}  // namespace

C5R_TEST_MAIN
