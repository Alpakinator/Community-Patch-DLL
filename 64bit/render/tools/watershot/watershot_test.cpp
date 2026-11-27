// watershot tests (epic 3600, issue 3604): the export reader, the replay and the comparator, on a synthetic export
// that the test writes from c5r::water's own ocean draw (the EXE's WaterShader programs from C5R_EXE; the files go to
// the test's scratch folder, C5R_WORK, never into the repository). A replay of that export must give the module's
// target pixel for pixel; every check of the reader has a negative test. Without C5R_EXE the export tests skip and
// render/build.py --test fails the run.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "c5r/bake/heightpage.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"
#include "c5r/water/water.h"
#include "testkit.h"
#include "watershot_core.h"

using namespace c5r;
using namespace c5r::watershot;

namespace {

std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void writeFile(const std::string& path, const void* data, size_t n) {
    std::ofstream f(path.c_str(), std::ios::binary);
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}
void writeText(const std::string& path, const std::string& s) { writeFile(path, s.data(), s.size()); }
void mkdirs(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i < p.size(); ++i) {
        cur.push_back(p[i]);
        if (p[i] == '/' || i + 1 == p.size()) CreateDirectoryA(cur.c_str(), nullptr);
    }
}
std::string root(const char* name) {
    const std::string r = test::workDir() + "/watershot_" + name;
    mkdirs(r + "/");
    return r;
}

// A texture file pair: tightly packed subresources.
void writeTex(const std::string& base, int format, uint32_t w, uint32_t h, uint32_t depth, uint32_t mips, const std::vector<uint8_t>& bytes,
              bool volume = false) {
    writeFile(base + ".bin", bytes.data(), bytes.size());
    char j[256];
    std::snprintf(j, sizeof j, "{\"format\": %d, \"width\": %u, \"height\": %u, \"depth\": %u, \"mips\": %u, \"array_size\": 1, \"dim\": \"%s\"}",
                  format, w, h, depth, mips, volume ? "3d" : "2d");
    writeText(base + ".json", j);
}

std::string samplerJson(uint32_t id) {
    const terrainsets::SamplerStateDesc* s = terrainsets::samplerState(id);
    char b[256];
    std::snprintf(b, sizeof b,
                  "{\"Filter\": %u, \"AddressU\": %u, \"AddressV\": %u, \"AddressW\": %u, \"MipLODBias\": 0, \"MaxAnisotropy\": %u, "
                  "\"ComparisonFunc\": %u, \"MinLOD\": 0, \"MaxLOD\": 3.402823466e+38}",
                  s->filter, s->address[0], s->address[1], s->address[2], s->maxAnisotropy ? s->maxAnisotropy : 1, s->comparison);
    return b;
}

struct Dev {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ~Dev() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
    bool create() {
        const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
        HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(h)) h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        return SUCCEEDED(h);
    }
};

ID3D11ShaderResourceView* tex2d(ID3D11Device* dev, DXGI_FORMAT f, uint32_t w, uint32_t h, const void* data, uint32_t pitch) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {data, pitch, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&d, &s, &t))) dev->CreateShaderResourceView(t, nullptr, &v);
    if (t) t->Release();
    return v;
}

heightfield::HeightGrid smallGrid() {
    heightfield::HeightGrid g;
    g.mapW = g.mapH = 8;
    g.samplesX = g.samplesY = 512;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = 256;
    g.oy = 256 * 0.866f;
    g.waterZ = 10.117f;
    g.h.resize(512 * 512);
    for (uint32_t y = 0; y < 512; ++y)
        for (uint32_t x = 0; x < 512; ++x) g.h[y * 512 + x] = static_cast<uint16_t>(((x * 7 + y * 3) & 0x3F) << 8);  // sea floor bytes 0..63
    return g;
}

// Writes a one-draw export of the module's own ocean draw, returns the module's result for comparison.
struct Synth {
    std::string dir;
    std::vector<uint8_t> after;
    uint32_t w = 96, h = 64;
    std::vector<uint8_t> frame;
    water::OceanGeometry geo;
    bool ok = false;
};

Synth makeExport(Dev& d, const std::string& exe) {
    Synth s;
    s.dir = root("export");
    mkdirs(s.dir + "/draws/0001_ocean/");
    mkdirs(s.dir + "/shaders/");
    mkdirs(s.dir + "/buffers/");
    mkdirs(s.dir + "/textures/");
    water::Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), L""));
    const heightfield::HeightGrid g = smallGrid();
    CHECK_OK(r.setHeightField(g));
    // inputs with known contents, written to the export as well
    struct In { water::Slot slot; DXGI_FORMAT f; std::vector<uint8_t> px; };
    std::vector<In> ins = {
        {water::Slot::Lean0, DXGI_FORMAT_R16G16B16A16_UNORM, {}}, {water::Slot::Lean1, DXGI_FORMAT_R16_UNORM, {}},
        {water::Slot::Reflection, DXGI_FORMAT_R8G8B8A8_UNORM, {}}, {water::Slot::Currents, DXGI_FORMAT_R8G8B8A8_UNORM, {}},
        {water::Slot::WaterColor, DXGI_FORMAT_R8G8B8A8_UNORM, {}}, {water::Slot::FogRamp, DXGI_FORMAT_R8G8B8A8_UNORM, {}},
        {water::Slot::Overlay, DXGI_FORMAT_R8G8B8A8_UNORM, {}},    {water::Slot::FowMask, DXGI_FORMAT_R8G8B8A8_UNORM, {}},
        {water::Slot::Shadow, DXGI_FORMAT_R8G8B8A8_UNORM, {}}};
    std::string srvJson;
    for (In& i : ins) {
        const uint32_t bpp = i.f == DXGI_FORMAT_R16G16B16A16_UNORM ? 8 : i.f == DXGI_FORMAT_R16_UNORM ? 2 : 4;
        i.px.resize(4 * bpp);
        for (size_t k = 0; k < i.px.size(); ++k) i.px[k] = static_cast<uint8_t>(37 * (k + 1) + 11 * static_cast<uint32_t>(i.slot));
        if (i.slot == water::Slot::FowMask) std::memset(i.px.data(), 0xFF, i.px.size());  // visible: no discard
        ID3D11ShaderResourceView* v = tex2d(d.dev, i.f, 2, 2, i.px.data(), 2 * bpp);
        CHECK(v != nullptr);
        CHECK_OK(r.setTexture(i.slot, v));
        if (v) v->Release();
        const std::string name = "textures/t" + std::to_string(static_cast<uint32_t>(i.slot));
        writeTex(s.dir + "/" + name, static_cast<int>(i.f), 2, 2, 1, 1, i.px);
        srvJson += (srvJson.empty() ? "" : ", ") + std::string("\"") + std::to_string(static_cast<uint32_t>(i.slot)) + "\": {\"file\": \"" + name + ".bin\"}";
    }
    {
        // t11: a 2x2x2 volume; the module's identity LUT replaced by it
        std::vector<uint8_t> v(2 * 2 * 2 * 4);
        for (size_t k = 0; k < v.size(); ++k) v[k] = static_cast<uint8_t>(k * 29 + 5);
        D3D11_TEXTURE3D_DESC dd = {};
        dd.Width = dd.Height = dd.Depth = 2;
        dd.MipLevels = 1;
        dd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dd.Usage = D3D11_USAGE_IMMUTABLE;
        dd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {v.data(), 8, 16};
        ID3D11Texture3D* t = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        CHECK(SUCCEEDED(d.dev->CreateTexture3D(&dd, &sd, &t)));
        if (t) d.dev->CreateShaderResourceView(t, nullptr, &srv);
        if (t) t->Release();
        CHECK_OK(r.setTexture(water::Slot::TransFow, srv));
        if (srv) srv->Release();
        writeTex(s.dir + "/textures/t11", DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 2, 1, v, true);
        srvJson += ", \"11\": {\"file\": \"textures/t11.bin\"}";
    }
    {
        // t7: the page the module binds (page 0 of the 8 x 8-plot field), all 11 levels
        bake::HeightPage page;
        CHECK_OK(bake::buildHeightPage(g, bake::HeightPageIndex(), bake::kHeightPageMips, page));
        std::vector<uint8_t> all;
        for (const auto& l : page.levels) all.insert(all.end(), l.begin(), l.end());
        writeTex(s.dir + "/textures/t7", DXGI_FORMAT_A8_UNORM, 1024, 1024, 1, bake::kHeightPageMips, all);
        srvJson += ", \"7\": {\"file\": \"textures/t7.bin\"}";
    }
    // the draw: a camera over the middle of the field, one page, one view
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = s.w;
    td.Height = s.h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &tex)));
    CHECK(SUCCEEDED(d.dev->CreateRenderTargetView(tex, nullptr, &rtv)));
    std::vector<uint8_t> before(static_cast<size_t>(s.w) * s.h * 4);
    for (size_t k = 0; k < before.size(); ++k) before[k] = static_cast<uint8_t>((k * 13) & 0xFF);
    d.ctx->UpdateSubresource(tex, 0, nullptr, before.data(), s.w * 4, 0);
    water::FrameInputs in;
    const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -400, 1};
    const float ys = 1.0f / std::tan(15.0f * 3.14159265f / 180.0f);
    const float proj[16] = {ys * 64 / 96, 0, 0, 0, 0, ys, 0, 0, 0, 0, -1.0002f, -1, 0, 0, -1.0002f, 0};
    std::memcpy(in.cam.view, view, sizeof view);
    std::memcpy(in.cam.proj, proj, sizeof proj);
    in.cam.width = s.w;
    in.cam.height = s.h;
    in.fields = r.stockFields();
    in.fields.set(0xE4, 0.006f);
    in.fields.set(0xDC, 0.15f);
    in.fog.density = 0.0005f;
    water::FrameStats st;
    CHECK_OK(r.draw(d.ctx, rtv, nullptr, in, water::Options(), &st));
    CHECK_EQ(st.oceanDraws, 1u);
    s.geo = r.oceanGeometry();
    s.frame = r.frames()[0];
    {
        D3D11_TEXTURE2D_DESC sd2 = td;
        sd2.Usage = D3D11_USAGE_STAGING;
        sd2.BindFlags = 0;
        sd2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* stg = nullptr;
        CHECK(SUCCEEDED(d.dev->CreateTexture2D(&sd2, nullptr, &stg)));
        d.ctx->CopyResource(stg, tex);
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(SUCCEEDED(d.ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m)));
        s.after.resize(before.size());
        for (uint32_t y = 0; y < s.h; ++y)
            std::memcpy(s.after.data() + static_cast<size_t>(y) * s.w * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, s.w * 4);
        d.ctx->Unmap(stg, 0);
        stg->Release();
    }
    rtv->Release();
    tex->Release();
    CHECK(s.after != before);
    // files
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(wide(exe)));
    const shaders::ShaderBlob* vs = sec.findByVa(0x06815a48);
    const shaders::ShaderBlob* ps = sec.findByVa(0x06820b00);  // pass 3
    CHECK(vs && ps);
    if (!vs || !ps) return s;
    writeFile(s.dir + "/shaders/vs.dxbc", vs->bytes, vs->size);
    writeFile(s.dir + "/shaders/ps.dxbc", ps->bytes, ps->size);
    writeFile(s.dir + "/buffers/vb.bin", s.geo.vertices.data(), s.geo.vertices.size() * 4);
    writeFile(s.dir + "/buffers/ib.bin", s.geo.indices.data(), s.geo.indices.size() * 2);
    writeFile(s.dir + "/buffers/cb1.bin", s.frame.data(), s.frame.size());
    std::vector<uint8_t> own(0x80, 0);
    writeFile(s.dir + "/buffers/cb0.bin", own.data(), own.size());
    writeTex(s.dir + "/draws/0001_ocean/rt_before_0", DXGI_FORMAT_R8G8B8A8_UNORM, s.w, s.h, 1, 1, before);
    writeTex(s.dir + "/draws/0001_ocean/rt_after_0", DXGI_FORMAT_R8G8B8A8_UNORM, s.w, s.h, 1, 1, s.after);
    std::string samp;
    for (uint32_t i = 0; i < 14; ++i) {
        const uint32_t id = i == 6 ? 3 : (i == 8 || i == 11) ? 5 : 0;
        samp += (i ? ", " : "") + std::string("\"") + std::to_string(i) + "\": " + samplerJson(id);
    }
    const std::string cbs = "{\"0\": {\"size\": 128, \"file\": \"buffers/cb0.bin\"}, \"1\": {\"size\": 592, \"file\": \"buffers/cb1.bin\"}}";
    char head[512];
    std::snprintf(head, sizeof head,
                  "{\"call\": \"DrawIndexed\", \"args\": {\"IndexCountPerInstance\": %u, \"InstanceCount\": 1, \"StartIndexLocation\": 0, "
                  "\"BaseVertexLocation\": 0, \"StartInstanceLocation\": 0},\n",
                  s.geo.quads * 6);
    std::string state = head;
    state += " \"ia\": {\"topology\": 4, \"elements\": [{\"SemanticName\": \"POSITION\", \"SemanticIndex\": 0, \"Format\": 2, \"InputSlot\": 0, "
             "\"AlignedByteOffset\": 0, \"InputSlotClass\": 0, \"InstanceDataStepRate\": 0}], "
             "\"vb\": {\"0\": {\"stride\": 16, \"offset\": 0, \"file\": \"buffers/vb.bin\"}}, \"ib\": {\"format\": 57, \"offset\": 0, \"file\": \"buffers/ib.bin\"}},\n";
    state += " \"vs\": {\"file\": \"shaders/vs.dxbc\", \"cb\": " + cbs + "},\n";
    state += " \"ps\": {\"file\": \"shaders/ps.dxbc\", \"cb\": " + cbs + ", \"srv\": {" + srvJson + "}, \"samp\": {" + samp + "}},\n";
    state += " \"hs\": null, \"ds\": null, \"gs\": null,\n";
    state += " \"om\": {\"blend\": {\"AlphaToCoverageEnable\": false, \"IndependentBlendEnable\": false, \"RenderTarget\": [{\"BlendEnable\": true, "
             "\"SrcBlend\": 5, \"DestBlend\": 6, \"BlendOp\": 1, \"SrcBlendAlpha\": 2, \"DestBlendAlpha\": 2, \"BlendOpAlpha\": 1, "
             "\"RenderTargetWriteMask\": 15}], \"BlendFactor\": [0, 0, 0, 0], \"SampleMask\": 4294967295}, "
             "\"depth_stencil\": {\"DepthEnable\": true, \"DepthWriteMask\": 0, \"DepthFunc\": 4, \"StencilEnable\": false}, ";
    char rt[160];
    std::snprintf(rt, sizeof rt, "\"rt\": [{\"res\": 1, \"format\": 28, \"w\": %u, \"h\": %u}], \"dsv\": null},\n", s.w, s.h);
    state += rt;
    char rs[320];
    std::snprintf(rs, sizeof rs,
                  " \"rs\": {\"rasterizer\": {\"FillMode\": 3, \"CullMode\": 2, \"FrontCounterClockwise\": true, \"DepthClipEnable\": true, "
                  "\"ScissorEnable\": false}, \"viewports\": [{\"TopLeftX\": 0, \"TopLeftY\": 0, \"Width\": %u, \"Height\": %u, \"MinDepth\": 0, "
                  "\"MaxDepth\": 1}]}}\n",
                  s.w, s.h);
    state += rs;
    writeText(s.dir + "/draws/0001_ocean/state.json", state);
    // The same draw in the shape 3601's exporter writes (seen in its first export, 2026-11-14 00:40): enums as
    // {"v": n, "s": name}, contents under "data", stages "VS"/"PS" with "shader", state objects {id, desc},
    // render targets "rtv" with view_desc / res_desc; unclassified ("candidate").
    mkdirs(s.dir + "/draws/0002_candidate/");
    writeTex(s.dir + "/draws/0002_candidate/rt_before_0", DXGI_FORMAT_R8G8B8A8_UNORM, s.w, s.h, 1, 1, before);
    writeTex(s.dir + "/draws/0002_candidate/rt_after_0", DXGI_FORMAT_R8G8B8A8_UNORM, s.w, s.h, 1, 1, s.after);
    auto ev = [](uint32_t v) { return "{\"v\": " + std::to_string(v) + ", \"s\": \"x\"}"; };
    auto dataFile = [](const std::string& f, uint32_t size) {
        return "\"data\": {\"res\": 9, \"desc\": null, \"size\": " + std::to_string(size) + ", \"file\": \"" + f + "\", \"written_in_frame\": false}";
    };
    const std::string cbs2 = "{\"0\": {\"res\": 1, \"first\": null, \"num\": null, " + dataFile("buffers/cb0.bin", 128) + "}, \"1\": {\"res\": 2, " +
                             dataFile("buffers/cb1.bin", 592) + "}}";
    std::string srv2, samp2;
    for (uint32_t slot : {0u, 1u, 4u, 5u, 6u, 8u, 9u, 10u, 13u, 11u, 7u}) {
        const uint32_t fmt = slot == 0 ? 11u : slot == 1 ? 56u : slot == 7 ? 65u : 28u;
        srv2 += (srv2.empty() ? "" : ", ") + std::string("\"") + std::to_string(slot) + "\": {\"view\": 5, \"view_desc\": {\"Format\": " + ev(fmt) +
                "}, \"res\": 6, " + dataFile("textures/t" + std::to_string(slot) + ".bin", 0) + "}";
    }
    for (uint32_t i = 0; i < 14; ++i) {
        const uint32_t id = i == 6 ? 3 : (i == 8 || i == 11) ? 5 : 0;
        samp2 += (i ? ", " : "") + std::string("\"") + std::to_string(i) + "\": {\"id\": 77, \"desc\": " + samplerJson(id) + "}";
    }
    std::string st2 = head;
    st2 += " \"ia\": {\"topology\": " + ev(4) + ", \"layout\": [{\"SemanticName\": \"POSITION\", \"SemanticIndex\": 0, \"Format\": " + ev(2) +
           ", \"InputSlot\": 0, \"AlignedByteOffset\": 0, \"InputSlotClass\": " + ev(0) + ", \"InstanceDataStepRate\": 0}], "
           "\"vb\": {\"0\": {\"res\": 3, \"stride\": 16, \"offset\": 0, " + dataFile("buffers/vb.bin", 0) + "}}, "
           "\"ib\": {\"res\": 4, \"format\": " + ev(57) + ", \"offset\": 0, " + dataFile("buffers/ib.bin", 0) + "}},\n";
    st2 += " \"VS\": {\"shader\": {\"res\": 1, \"kind\": \"VertexShader\", \"file\": \"shaders/vs.dxbc\"}, \"cb\": " + cbs2 + ", \"srv\": {}, \"samp\": {}},\n";
    st2 += " \"HS\": {\"shader\": null, \"cb\": {}, \"srv\": {}, \"samp\": {}},\n";
    st2 += " \"PS\": {\"shader\": {\"res\": 2, \"kind\": \"PixelShader\", \"file\": \"shaders/ps.dxbc\"}, \"cb\": " + cbs2 + ", \"srv\": {" + srv2 +
           "}, \"samp\": {" + samp2 + "}},\n";
    st2 += " \"om\": {\"rtv\": [{\"view\": 8, \"view_desc\": {\"Format\": " + ev(28) + "}, \"res\": 9, \"res_desc\": {\"Width\": " + std::to_string(s.w) +
           ", \"Height\": " + std::to_string(s.h) + "}}], \"dsv\": null, "
           "\"blend\": {\"id\": 854, \"desc\": {\"AlphaToCoverageEnable\": false, \"IndependentBlendEnable\": false, \"RenderTarget\": [{\"BlendEnable\": true, "
           "\"SrcBlend\": " + ev(5) + ", \"DestBlend\": " + ev(6) + ", \"BlendOp\": " + ev(1) + ", \"SrcBlendAlpha\": " + ev(2) + ", \"DestBlendAlpha\": " +
           ev(2) + ", \"BlendOpAlpha\": " + ev(1) + ", \"RenderTargetWriteMask\": 15}]}, \"factor\": [0, 0, 0, 0], \"sample_mask\": 4294967295}, "
           "\"depth\": {\"id\": 860, \"desc\": {\"DepthEnable\": true, \"DepthWriteMask\": " + ev(0) + ", \"DepthFunc\": " + ev(4) +
           ", \"StencilEnable\": false}, \"stencil_ref\": 0}},\n";
    st2 += " \"rs\": {\"state\": {\"id\": 866, \"desc\": {\"FillMode\": " + ev(3) + ", \"CullMode\": " + ev(2) +
           ", \"FrontCounterClockwise\": true, \"DepthClipEnable\": true, \"ScissorEnable\": false}}, \"viewports\": [{\"TopLeftX\": 0.0, "
           "\"TopLeftY\": 0.0, \"Width\": " + std::to_string(s.w) + ".0, \"Height\": " + std::to_string(s.h) + ".0, \"MinDepth\": 0.0, \"MaxDepth\": 1.0}], "
           "\"scissors\": []}}\n";
    writeText(s.dir + "/draws/0002_candidate/state.json", st2);
    writeText(s.dir + "/draws.json",
              "{\"version\": 1, \"capture\": \"synthetic\", \"replay\": true, \"draws\": [{\"n\": 1, \"chunk\": 7, \"system\": \"ocean\", "
              "\"dir\": \"draws/0001_ocean\", \"call\": \"DrawIndexed\"}, {\"n\": 2, \"chunk\": 8, \"system\": \"candidate\", "
              "\"dir\": \"draws/0002_candidate\", \"call\": \"DrawIndexedInstanced\"}]}");
    s.ok = true;
    return s;
}

} // namespace

C5R_TEST(compare_counts_and_classes) {
    std::vector<uint8_t> a(4 * 4 * 4, 100), b = a;
    b[0] = 101;        // d 1
    b[4 + 1] = 98;     // d 2
    b[8 + 2] = 104;    // d 4
    b[12 + 3] = 140;   // d 40
    b[16] = 0;         // d 100
    Compare c;
    CHECK_OK(compareRgba8(a.data(), 16, b.data(), 16, 4, 4, c));
    CHECK_EQ(c.total, 16u);
    CHECK_EQ(c.equal, 11u);
    CHECK_EQ(c.hist[0], 1u);
    CHECK_EQ(c.hist[1], 1u);
    CHECK_EQ(c.hist[2], 1u);
    CHECK_EQ(c.hist[6], 1u);
    CHECK_EQ(c.hist[7], 1u);
    CHECK_EQ(c.maxDiff, 100);
    CHECK_EQ(c.p50, 4);
    CHECK(describe(c).find("equal 11 / 16") != std::string::npos);
    CHECK_ERR(compareRgba8(a.data(), 15, b.data(), 16, 4, 4, c), Err::BadLayout);
    CHECK_ERR(compareRgba8(a.data(), 16, b.data(), 16, 0, 4, c), Err::BadLayout);
    CHECK_ERR(compareRgba8(nullptr, 16, b.data(), 16, 4, 4, c), Err::BadLayout);
}

C5R_TEST(reader_rejects_bad_exports) {
    const std::string r = root("bad");
    ExportIndex idx;
    CHECK_ERR(loadIndex(wide(r + "/none"), idx), Err::Io);
    writeText(r + "/draws.json", "{\"version\": 2, \"draws\": []}");
    CHECK_ERR(loadIndex(wide(r), idx), Err::BadVersion);
    writeText(r + "/draws.json", "{\"version\": 1, \"draws\": [{\"n\": 1, \"system\": \"ocean\"}]}");
    CHECK_ERR(loadIndex(wide(r), idx), Err::NotFound);
    writeText(r + "/draws.json", "{\"version\": 1, \"draws\": [{\"n\": 1, \"system\": \"ocean\", \"dir\": \"../x\"}]}");
    CHECK_ERR(loadIndex(wide(r), idx), Err::BadLayout);
    writeText(r + "/draws.json", "{\"version\": 1, \"draws\": [{\"n\": 1, \"system\": \"ocean\", \"dir\": \"C:/x\"}]}");
    CHECK_ERR(loadIndex(wide(r), idx), Err::BadLayout);
    writeText(r + "/draws.json", "{\"version\": 1, \"draws\": [");
    CHECK(!loadIndex(wide(r), idx).ok());
    writeText(r + "/draws.json", "{\"version\": 1, \"draws\": [{\"n\": 3, \"system\": \"ocean\", \"dir\": \"d3\"}]}");
    CHECK_OK(loadIndex(wide(r), idx));
    CHECK_EQ(idx.draws.size(), size_t(1));
    // textures
    TexFile t;
    CHECK_ERR(loadTexture(wide(r + "/none.bin"), t), Err::Io);
    writeText(r + "/t.json", "{\"width\": 2, \"height\": 2}");
    writeFile(r + "/t.bin", "abcdefghijklmnop", 16);
    CHECK_ERR(loadTexture(wide(r + "/t.bin"), t), Err::NotFound);
    writeText(r + "/t.json", "{\"format\": 999, \"width\": 2, \"height\": 2}");
    CHECK_ERR(loadTexture(wide(r + "/t.bin"), t), Err::Unsupported);
    writeText(r + "/t.json", "{\"format\": 28, \"width\": 0, \"height\": 2}");
    CHECK_ERR(loadTexture(wide(r + "/t.bin"), t), Err::Unsupported);
    writeText(r + "/t.json", "{\"format\": 28, \"width\": 4, \"height\": 2}");
    CHECK_ERR(loadTexture(wide(r + "/t.bin"), t), Err::Truncated);
    writeText(r + "/t.json", "{\"format\": 28, \"width\": 2, \"height\": 2, \"subresources\": [{\"offset\": 0}, {\"offset\": 0}]}");
    CHECK_ERR(loadTexture(wide(r + "/t.bin"), t), Err::BadLayout);
    writeText(r + "/t.json", "{\"format\": 28, \"width\": 2, \"height\": 2}");
    CHECK_OK(loadTexture(wide(r + "/t.bin"), t));
    CHECK_EQ(t.subs[0].rowPitch, 8u);
    // draw states
    CapturedDraw s;
    DrawEntry e;
    e.dir = "d3";
    DeleteFileA((r + "/d3/state.json").c_str());  // a previous run's file
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::Io);
    mkdirs(r + "/d3/");
    writeText(r + "/d3/state.json", "{\"args\": {}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::NotFound);  // no call
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {\"VertexCountPerInstance\": 3}, \"ia\": {}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::NotFound);  // no topology
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {\"InstanceCount\": 2}, \"ia\": {\"topology\": 4}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::Unsupported);  // instanced
    writeText(r + "/d3/state.json", "{\"call\": \"DrawIndexed\", \"args\": {\"IndexCountPerInstance\": 3}, \"ia\": {\"topology\": 4}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::NotFound);  // no index buffer
    writeFile(r + "/ib.bin", "\0\0\1\0", 4);
    writeText(r + "/d3/state.json", "{\"call\": \"DrawIndexed\", \"args\": {\"IndexCountPerInstance\": 3}, \"ia\": {\"topology\": 4, \"ib\": {\"format\": 57, \"file\": \"ib.bin\"}}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::Truncated);  // 3 indices, 2 in the file
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {}, \"ia\": {\"topology\": 4}, \"hs\": {\"file\": \"x.dxbc\"}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::Unsupported);
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {}, \"ia\": {\"topology\": 4}, \"vs\": {\"file\": \"../x.dxbc\"}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::BadLayout);
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {}, \"ia\": {\"topology\": 4}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::NotFound);  // no vs
    writeFile(r + "/x.dxbc", "DXBC", 4);
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {}, \"ia\": {\"topology\": 4}, \"vs\": {\"file\": \"x.dxbc\"}, \"ps\": {\"file\": \"x.dxbc\"}}");
    CHECK_ERR(loadDrawState(wide(r), e, s), Err::NotFound);  // no render target
    writeText(r + "/d3/state.json", "{\"call\": \"Draw\", \"args\": {}, \"ia\": {\"topology\": 4}, \"vs\": {\"file\": \"x.dxbc\"}, \"ps\": {\"file\": \"x.dxbc\"}, "
                                     "\"om\": {\"rt\": [{\"format\": 28, \"w\": 4, \"h\": 4}]}}");
    CHECK_OK(loadDrawState(wide(r), e, s));
    CHECK(!s.haveAfter);
    std::vector<uint8_t> f;
    CHECK_ERR(capturedWaterFrame(s, f), Err::NotFound);
    Dev d;
    CHECK(d.create());
    ReplayResult rr;
    s.rtFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    CHECK_ERR(replayDraw(d.dev, d.ctx, s, nullptr, rr), Err::Unsupported);
    CHECK_ERR(replayDraw(nullptr, nullptr, s, nullptr, rr), Err::BadLayout);
    s.rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    CHECK_ERR(replayDraw(d.dev, d.ctx, s, nullptr, rr), Err::Device);  // "DXBC" is not a shader
}

C5R_TEST(replay_of_a_synthetic_export_is_pixel_exact) {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set: the export is made from the EXE's WaterShader programs");
    Dev d;
    CHECK(d.create());
    Synth s = makeExport(d, exe);
    if (!s.ok) return;
    ExportIndex idx;
    CHECK_OK(loadIndex(wide(s.dir), idx));
    CHECK(idx.replay);
    CHECK_EQ(idx.draws.size(), size_t(2));
    // the exporter's own shape replays the same pixels, and its VS is the EXE's ocean VS
    {
        CapturedDraw n2;
        CHECK_OK(loadDrawState(wide(s.dir), idx.draws[1], n2));
        CHECK_EQ(n2.ps.srvs.size(), size_t(11));
        CHECK_EQ(n2.topology, 4u);
        CHECK_EQ(static_cast<uint32_t>(n2.rtFormat), 28u);
        CHECK_EQ(n2.rtWidth, s.w);
        CHECK(n2.blend.RenderTarget[0].BlendEnable);
        ReplayResult r2;
        CHECK_OK(replayDraw(d.dev, d.ctx, n2, nullptr, r2));
        CHECK(r2.compared);
        CHECK_EQ(r2.cmp.equal, r2.cmp.total);
        std::vector<uint8_t> code;
        CHECK_OK(drawVertexShader(wide(s.dir), idx.draws[1], code));
        shaders::ShaderSection sec;
        CHECK_OK(sec.open(wide(exe)));
        const shaders::ShaderBlob* vsb = sec.findByVa(0x06815a48);
        CHECK(vsb && code.size() == vsb->size && std::memcmp(code.data(), vsb->bytes, code.size()) == 0);
    }
    CapturedDraw st;
    CHECK_OK(loadDrawState(wide(s.dir), idx.draws[0], st));
    CHECK(st.haveBefore && st.haveAfter);
    CHECK_EQ(st.ps.srvs.size(), size_t(11));
    CHECK_EQ(st.ps.samps.size(), size_t(14));
    ReplayResult rr;
    CHECK_OK(replayDraw(d.dev, d.ctx, st, nullptr, rr));
    CHECK(rr.compared);
    if (rr.cmp.equal != rr.cmp.total) std::printf("  replay: %s\n", describe(rr.cmp).c_str());
    CHECK_EQ(rr.cmp.equal, rr.cmp.total);
    CHECK_EQ(rr.cmp.total, uint64_t(s.w) * s.h);
    // replayed 4x multisampled and resolved (the stock target's kind): equal inside the water, edges may differ
    {
        CapturedDraw ms = st;
        ms.rtSamples = 4;
        ReplayResult rm;
        CHECK_OK(replayDraw(d.dev, d.ctx, ms, nullptr, rm));
        CHECK(rm.compared);
        std::printf("  4x MSAA replay against the single-sampled draw: %s\n", describe(rm.cmp).c_str());
        CHECK(rm.cmp.equal * 100 >= rm.cmp.total * 90);
    }
    // the module's parts in place of the export's: the same pixels
    ModuleParts mp;
    mp.frame = s.frame.data();
    mp.vertices = &s.geo.vertices;
    mp.indices = &s.geo.indices;
    mp.startIndex = 0;
    mp.indexCount = s.geo.quads * 6;
    CHECK_OK(replayDraw(d.dev, d.ctx, st, &mp, rr));
    CHECK_EQ(rr.cmp.equal, rr.cmp.total);
    // a changed WaterFrame (another water height) changes pixels: the comparison sees it
    std::vector<uint8_t> f2 = s.frame;
    float z;
    std::memcpy(&z, f2.data() + 0xC0, 4);
    z += 40.0f;
    std::memcpy(f2.data() + 0xC0, &z, 4);
    mp.frame = f2.data();
    CHECK_OK(replayDraw(d.dev, d.ctx, st, &mp, rr));
    CHECK(rr.cmp.equal < rr.cmp.total);
    // the frame check on the module's own block: every computed member equal
    std::vector<uint8_t> cf;
    CHECK_OK(capturedWaterFrame(st, cf));
    water::Renderer r;
    CHECK_OK(r.init(d.dev, wide(exe), L""));
    const water::World w = water::worldFromGrid(smallGrid());
    std::vector<MemberCheck> checks;
    water::FrameInputs used;
    CHECK_OK(checkWaterFrame(cf.data(), r.stockFields(), &w, checks, &used));
    CHECK_EQ(checks.size(), size_t(22));
    for (const MemberCheck& c : checks) {
        if (!c.equal) std::printf("  member %s differs (max relative %g)\n", c.name.c_str(), c.maxRelDiff);
        CHECK(c.equal);
    }
    // and with the world solved from the block itself [I]: the members that do not depend on the samples agree
    CHECK_OK(checkWaterFrame(cf.data(), r.stockFields(), nullptr, checks, &used));
    CHECK(std::fabs(used.world.width - w.width) < 1e-2f);
    water::WaterFields nf;
    CHECK_ERR(checkWaterFrame(cf.data(), nf, &w, checks, nullptr), Err::NotFound);
    CHECK_ERR(checkWaterFrame(nullptr, r.stockFields(), &w, checks, nullptr), Err::BadLayout);
}

C5R_TEST_MAIN
