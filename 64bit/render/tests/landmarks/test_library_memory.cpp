// test_library_memory.cpp - ticket 0581: the model library's CPU copies after the GPU upload. ModelLibrary::release
// drops one file's copy and lowers the counts; a later resident() makes it again byte-equal; the wire index built from
// copies made again is the same; ModelRenderer::prepare releases the file's copy once its IMMUTABLE buffers are made
// (city-renderer.md 12.10). The install, the database and the EXE are read at run time (skipped without them, which
// build.py counts as a failure).
#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/draw.h"
#include "c5r/landmarks/tap.h"
#include "landmarks_testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    Device() {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                     &got, &ctx)))
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
    }
    ~Device() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

uint64_t bytesOf(const ModelLibrary::Buffers& b) { return b.vertices.size() + b.skinnedVertices.size() + b.indices.size(); }

}  // namespace

C5R_TEST(release_drops_one_copy_and_remakes_it_equal) {
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    if (!in.libStatus.ok()) return;
    ModelLibrary& lib = in.lib;
    lib.evictAll();
    const int32_t c = lib.fileIndex("colossus.gr2", 0.05f);
    const int32_t e = lib.fileIndex("euro_anc_city.gr2", 1.0f);
    CHECK(c >= 0 && e >= 0);
    if (c < 0 || e < 0) return;
    const ModelLibrary::Buffers* bc = nullptr;
    const ModelLibrary::Buffers* be = nullptr;
    CHECK_OK(lib.resident(static_cast<uint32_t>(c), &bc));
    CHECK_OK(lib.resident(static_cast<uint32_t>(e), &be));
    if (!bc || !be) return;
    const ModelLibrary::Buffers keep = *bc;
    const uint64_t both = lib.residentBytes();
    CHECK_EQ(both, bytesOf(*bc) + bytesOf(*be));
    CHECK_EQ(lib.residentFiles(), 2u);

    lib.release(static_cast<uint32_t>(c));
    CHECK(!lib.isResident(static_cast<uint32_t>(c)));
    CHECK(lib.isResident(static_cast<uint32_t>(e)));          // the other file untouched
    CHECK_EQ(lib.residentFiles(), 1u);
    CHECK_EQ(lib.residentBytes(), bytesOf(*be));
    // no-ops: released twice, out of range, never resident
    lib.release(static_cast<uint32_t>(c));
    lib.release(static_cast<uint32_t>(lib.files().size()));
    lib.release(0xFFFFFFFFu);
    CHECK_EQ(lib.residentFiles(), 1u);
    CHECK_EQ(lib.residentBytes(), bytesOf(*be));

    // made again on demand, byte-equal (the repack is deterministic)
    const ModelLibrary::Buffers* again = nullptr;
    CHECK_OK(lib.resident(static_cast<uint32_t>(c), &again));
    CHECK(again != nullptr);
    if (!again) return;
    CHECK(again->vertices == keep.vertices);
    CHECK(again->skinnedVertices == keep.skinnedVertices);
    CHECK(again->indices == keep.indices);
    CHECK_EQ(lib.residentBytes(), both);
    CHECK_EQ(lib.residentFiles(), 2u);
    lib.evictAll();
    CHECK_EQ(lib.residentBytes(), 0u);
}

// The server's order (srv_model_layer_c5r.cpp open): WireIndex::build makes every copy; the renderer then releases each
// file's after its upload. A second index built from copies made again carries the same keys, so a file released and
// made again (a device re-made) is still found by the same wire keys.
C5R_TEST(wire_index_same_after_release) {
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    if (!in.libStatus.ok()) return;
    ModelLibrary& lib = in.lib;
    lib.evictAll();
    WireIndex a(lib, in.files);
    CHECK_OK(a.build());
    const uint32_t files = lib.residentFiles();
    const uint64_t bytes = lib.residentBytes();
    CHECK(files > 0);
    std::printf("after the wire index: %u files resident, %.1f MiB\n", files, bytes / 1048576.0);
    for (uint32_t f = 0; f < lib.files().size(); ++f) lib.release(f);
    CHECK_EQ(lib.residentFiles(), 0u);
    CHECK_EQ(lib.residentBytes(), 0u);
    // what a device re-made costs on the CPU side: every released file read and repacked again (an indication of the
    // lazy re-read on a rebuild, never a timing claim)
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t refused = 0;
    for (uint32_t f = 0; f < lib.files().size(); ++f)
        if (lib.files()[f].ok && !lib.resident(f, nullptr).ok()) ++refused;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("re-read of every released file: %u files, %.1f MiB, %.0f ms (an indication)\n", lib.residentFiles(),
                lib.residentBytes() / 1048576.0, ms);
    CHECK(refused <= a.failedFiles());
    CHECK_EQ(lib.residentBytes(), bytes);
    for (uint32_t f = 0; f < lib.files().size(); ++f) lib.release(f);
    WireIndex b(lib, in.files);
    CHECK_OK(b.build());
    CHECK_EQ(lib.residentFiles(), files);
    CHECK_EQ(lib.residentBytes(), bytes);
    CHECK_EQ(a.failedFiles(), b.failedFiles());
    uint32_t parts = 0, differ = 0;
    for (uint32_t f = 0; f < lib.files().size(); ++f)
        for (uint32_t p = 0; p < lib.files()[f].layout.parts.size(); ++p) {
            const WireKey* ka = a.key(f, p);
            const WireKey* kb = b.key(f, p);
            if (!ka && !kb) continue;
            ++parts;
            if (!ka || !kb || ka->ib != kb->ib || ka->vb != kb->vb || ka->t0 != kb->t0 || ka->t1 != kb->t1 ||
                ka->startIndex != kb->startIndex || ka->indexCount != kb->indexCount || ka->baseVertex != kb->baseVertex)
                ++differ;
        }
    std::printf("wire keys compared: %u parts, %u differ\n", parts, differ);
    CHECK(parts > 0);
    CHECK_EQ(differ, 0u);
    lib.evictAll();
}

namespace {

// One draw of a file's part 0 as Material::Blocks (as test_draw's draw_one_block) into 64 x 64 colour and id targets;
// the pixels read back. box: part 0's lo xyz, hi xyz.
bool drawBlock(Device& d, ModelRenderer& r, uint32_t fi, const float box[6], std::vector<uint32_t>& colour,
               std::vector<uint32_t>& ids) {
    const float* lo = box;
    const float* hi = box + 3;
    const float e = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 0.6f + 1e-3f, ez = (hi[2] - lo[2]) * 2.0f + 1e-3f;
    ModelScene s;
    s.targets.push_back(TapTarget{64, 64, 1, 28, 64, 64, 1});
    s.rows.resize(4 + 22);
    s.rows[0] = {1, 0, 0, 0};
    s.rows[1] = {0, 1, 0, 0};
    s.rows[2] = {0, 0, 1, 0};
    s.rows[3] = {-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2] + 1e-3f, 1};
    s.rows[4] = {1 / e, 0, 0, 0};
    s.rows[5] = {0, 1 / e, 0, 0};
    s.rows[6] = {0, 0, 1 / ez, 0};
    s.rows[7] = {0, 0, 0, 1};
    s.consts.push_back(TapConst{4, 22, 1, 1});
    DrawItem it;
    it.file = fi;
    it.part = 0;
    it.material = Material::Blocks;
    it.instanceCount = 1;
    it.instRow = 0;
    it.vsCb1 = it.psCb1 = 0;
    s.items.push_back(it);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D11Texture2D *rt = nullptr, *idt = nullptr, *dt = nullptr;
    ID3D11RenderTargetView *rtv = nullptr, *idv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    bool ok = SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &rt)) &&
              SUCCEEDED(d.dev->CreateRenderTargetView(rt, nullptr, &rtv));
    td.Format = DXGI_FORMAT_R32_UINT;
    ok = ok && SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &idt)) &&
         SUCCEEDED(d.dev->CreateRenderTargetView(idt, nullptr, &idv));
    td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ok = ok && SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &dt)) &&
         SUCCEEDED(d.dev->CreateDepthStencilView(dt, nullptr, &dsv));
    if (ok) {
        const float clear[4] = {0, 0, 0, 0};
        d.ctx->ClearRenderTargetView(rtv, clear);
        d.ctx->ClearRenderTargetView(idv, clear);
        d.ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ViewInput v;
        v.rtv = rtv;
        v.dsv = dsv;
        v.idRtv = idv;
        v.viewport = {0, 0, 64, 64, 0, 1};
        EnvInput env;
        DrawReport rep;
        ok = r.draw(d.ctx, s, v, env, &rep).ok() && rep.drawn == 1;
    }
    for (int k = 0; ok && k < 2; ++k) {
        ID3D11Texture2D* src = k == 0 ? rt : idt;
        std::vector<uint32_t>& out = k == 0 ? colour : ids;
        D3D11_TEXTURE2D_DESC sd;
        src->GetDesc(&sd);
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        ok = SUCCEEDED(d.dev->CreateTexture2D(&sd, nullptr, &st));
        if (!ok) break;
        d.ctx->CopyResource(st, src);
        D3D11_MAPPED_SUBRESOURCE ms;
        ok = SUCCEEDED(d.ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms));
        if (ok) {
            out.assign(64 * 64, 0);
            for (uint32_t y = 0; y < 64; ++y)
                std::memcpy(out.data() + y * 64, static_cast<const uint8_t*>(ms.pData) + y * ms.RowPitch, 64 * 4);
            d.ctx->Unmap(st, 0);
        }
        st->Release();
    }
    for (IUnknown* u : std::initializer_list<IUnknown*>{rtv, idv, dsv, rt, idt, dt})
        if (u) u->Release();
    return ok;
}

uint64_t hashOf(const ModelLibrary::Buffers& b) {
    return fnv1a64(b.indices.data(), b.indices.size(),
                   fnv1a64(b.skinnedVertices.data(), b.skinnedVertices.size(),
                           fnv1a64(b.vertices.data(), b.vertices.size())));
}

}  // namespace

// ModelRenderer::prepare uploads a file's buffers and releases its CPU copy; drawing needs only the GPU buffers. A
// renderer made again (the server's setDevice after a device is lost and rebuilt, D213) re-reads the released file,
// uploads the same bytes (hash-equal) and draws the same pixels.
C5R_TEST(prepare_releases_the_copy_and_a_new_renderer_reuploads_it) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set");
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    if (!in.libStatus.ok()) return;
    Device d;
    if (!d.dev) test::skip("no D3D11 device");
    BuildingPrograms prog;
    CHECK_OK(prog.open(gamefs::fromUtf8(exe)));
    ModelLibrary& lib = in.lib;
    lib.evictAll();
    const int32_t fi = lib.fileIndex("euro_anc_city.gr2", 1.0f);
    CHECK(fi >= 0);
    if (fi < 0) return;
    const uint32_t f = static_cast<uint32_t>(fi);
    // the copy as the wire index leaves it: its hash and part 0's box
    const ModelLibrary::Buffers* b = nullptr;
    CHECK_OK(lib.resident(f, &b));
    if (!b) return;
    const uint64_t h0 = hashOf(*b);
    const Part& p = lib.files()[f].layout.parts[0];
    float box[6] = {1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    const uint16_t* ix = reinterpret_cast<const uint16_t*>(b->indices.data());
    for (uint32_t k = 0; k < p.key.indexCount; ++k) {
        const uint32_t v = ix[p.key.firstIndex + k] + static_cast<uint32_t>(p.key.baseVertex);
        float pos[3];
        std::memcpy(pos, b->vertices.data() + static_cast<size_t>(v) * 32, 12);
        for (int c = 0; c < 3; ++c) {
            box[c] = std::min(box[c], pos[c]);
            box[3 + c] = std::max(box[3 + c], pos[c]);
        }
    }
    b = nullptr;

    std::vector<uint32_t> colour1, ids1, colour2, ids2;
    uint64_t gpu1 = 0;
    {
        ModelRenderer r;
        CHECK_OK(r.init(d.dev, prog, lib, in.files));
        CHECK_OK(r.prepare(f));
        CHECK(r.prepared(f));
        CHECK(!lib.isResident(f));   // released after the upload
        CHECK_EQ(lib.residentBytes(), 0u);
        gpu1 = r.gpuBytes();
        CHECK(gpu1 > 0);
        CHECK(drawBlock(d, r, f, box, colour1, ids1));   // drawn from the GPU buffers alone
        r.release();
    }
    {
        ModelRenderer r;   // a device made again: the copy is read and repacked again, uploaded and released
        CHECK_OK(r.init(d.dev, prog, lib, in.files));
        const auto t0 = std::chrono::steady_clock::now();
        CHECK_OK(r.prepare(f));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("re-init prepare of %s (re-read, repack, upload, textures): %.1f ms (an indication)\n",
                    lib.files()[f].gr2.c_str(), ms);
        CHECK(!lib.isResident(f));
        CHECK_EQ(r.gpuBytes(), gpu1);
        CHECK(drawBlock(d, r, f, box, colour2, ids2));
        r.release();
    }
    uint32_t covered = 0;
    for (uint32_t id : ids1) covered += id == 1;
    CHECK(covered > 100);
    CHECK(colour1 == colour2);
    CHECK(ids1 == ids2);
    // the bytes made again hash the same as the first upload's
    CHECK_OK(lib.resident(f, &b));
    if (b) CHECK_EQ(hashOf(*b), h0);
    lib.evictAll();
}

C5R_TEST_MAIN

