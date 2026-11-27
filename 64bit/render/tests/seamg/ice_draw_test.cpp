// c5r::ice renderer tests (issue 4306, epic 0169): (b) the module with the EXE's IceShader, the install's textures and
// task 4301's captured VB, IB and inputs on a hardware device: its programs, textures, layout and states equal the
// capture's (draw 171, `ref/stock_load1/draws/0171_m59p0/state.json`), its picture is deterministic (two devices equal);
// (c) every check of init, setMesh and draw, each asserting the status and that nothing half-made stays. D137: offline,
// no game client. Own code; the EXE, the install and the reference set are read at run time.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "c5r/gamefs/gamefiles.h"
#include "c5r/ice/ice.h"
#include "c5r/shaders/shadersection.h"
#include "ice_util.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::ice;
using namespace c5r::itest;

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

const char* const kVsFile = "stock_load1/shaders/1740_9a689742.dxbc";
const char* const kPsFile = "stock_load1/shaders/1748_24a6504a.dxbc";
const char* const kScene = "stock_load1/buffers/51172_9b6b504e.bin";
// The capture's views of t0-t6 (draw 171; frame-start contents of every subresource, tightly packed).
const char* const kOwnFiles[7] = {"stock_load1/textures/35964_dda9cfec.bin", "stock_load1/textures/35958_fbac59cf.bin",
                                  "stock_load1/textures/35967_b4b21701.bin", "stock_load1/textures/35970_246840b3.bin",
                                  "stock_load1/textures/35961_1f6f0178.bin", "stock_load1/textures/35972_b4b21701.bin",
                                  "stock_load1/textures/35975_246840b3.bin"};

std::wstring exeOrSkip() {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set: the IceShader programs come from the EXE");
    return wide(exe);
}
std::wstring gameOrSkip() {
    const std::string g = test::gameDir();
    if (g.empty()) test::skip("C5R_GAME_DIR not set: the ice's textures come from the install");
    return wide(g);
}

// The EXE's section and the install, opened once per test executable.
struct Inputs {
    shaders::ShaderSection sec;
    gamefs::GameFiles fs;
    bool ok = false;
};
Inputs& inputs() {
    static Inputs* in = nullptr;
    if (!in) {
        in = new Inputs();
        const std::wstring exe = exeOrSkip(), game = gameOrSkip();
        Status a = in->sec.open(exe), b = in->fs.open(game);
        CHECK_OK(a);
        CHECK_OK(b);
        in->ok = a.ok() && b.ok();
    }
    return *in;
}

// The capture's mesh: VB, IB, one chunk (the probe's box, 96,785 triangles, start 0, base 0), the Tiny fields.
bool capturedMesh(Mesh& m) {
    std::vector<uint8_t> scene;
    if (!readRef(kVbFile, m.vb, 489112) || !readRef(kIbFile, m.ib, 580710) || !readRef(kScene, scene, kIceFrameBytes))
        return false;
    if (!tinyFields(scene, m.fields)) return false;
    Chunk c;
    for (int i = 0; i < 4; ++i) c.box[i] = m.fields.get(field::kBox + 4 * i);
    c.triangles = kTinyTriangles;
    m.chunks.assign(1, c);
    return true;
}

// Draws the capture's draw 171 on a fresh hardware device into a w x h target; the picture in rgba.
struct Run {
    Device d;
    Renderer r;
    Target t;
    Shared sh;
    FrameInputs in;
    FrameStats st;
    bool go(uint32_t w, uint32_t h, std::vector<uint8_t>& rgba) {
        Inputs& g = inputs();
        if (!g.ok) return false;
        Status s = d.create();
        CHECK_OK(s);
        if (!s.ok()) return false;
        s = r.init(d.dev, g.sec, g.fs);
        CHECK_OK(s);
        if (!s.ok()) return false;
        Mesh m;
        if (!capturedMesh(m)) return false;
        CHECK_OK(r.setMesh(m));
        std::vector<uint8_t> scene;
        if (!readRef(kScene, scene, kIceFrameBytes) || !sh.make(d.dev, d.ctx)) return false;
        in = capturedInputs(scene, scene);
        in.cam.width = w;
        in.cam.height = h;
        sh.into(in);
        CHECK_OK(t.create(d.dev, w, h));
        t.clear(d.ctx);
        s = r.draw(d.ctx, t.rtv, t.dsv, in, &st);
        CHECK_OK(s);
        if (!s.ok()) return false;
        CHECK_OK(t.read(d.ctx, rgba));
        return true;
    }
};

uint32_t covered(const std::vector<uint8_t>& rgba) {
    uint32_t n = 0;
    for (size_t i = 0; i < rgba.size(); i += 4)
        if (rgba[i] | rgba[i + 1] | rgba[i + 2] | rgba[i + 3]) ++n;
    return n;
}

// A section holding these blobs (8-aligned, 4 zero bytes between where needed); their VAs in vas.
std::vector<uint8_t> sectionWith(const std::vector<std::vector<uint8_t>>& blobs, std::vector<uint32_t>& vas) {
    std::vector<uint8_t> s;
    vas.clear();
    for (const auto& b : blobs) {
        while (s.size() % 8) s.push_back(0);
        vas.push_back(0x400000u + 0x2000u + static_cast<uint32_t>(s.size()));
        s.insert(s.end(), b.begin(), b.end());
    }
    return test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                          {".shader", 0x2000, static_cast<uint32_t>(s.size()), s}});
}

std::vector<uint8_t> blobOf(const shaders::ShaderSection& sec, uint32_t va) {
    const shaders::ShaderBlob* b = sec.findByVa(va);
    return b ? std::vector<uint8_t>(b->bytes, b->bytes + b->size) : std::vector<uint8_t>();
}

// HLSL stand-ins with one difference each from the EXE's programs (compiled at test time, stripped like the EXE's).
const char* const kPsHead =
    "cbuffer F : register(b1) { float4 r[30]; };\n"
    "TextureCube t0 : register(t0); Texture2D t1 : register(t1); Texture2D t2 : register(t2);\n"
    "Texture2D t4 : register(t4); Texture2D t5 : register(t5); Texture2D t6 : register(t6); Texture2D t7 : register(t7);\n"
    "Texture2D t8 : register(t8); Texture2D t9 : register(t9); Texture3D t10 : register(t10);\n"
    "SamplerState s0 : register(s0);\n";
const char* const kPsBody =
    "float4 c = r[29] + r[0];\n"
    "c += t0.Sample(s0, c.xyz) + t1.Sample(s0, c.xy) + t2.Sample(s0, c.xy) + t4.Sample(s0, c.xy) + t5.Sample(s0, c.xy);\n"
    "c += t6.Sample(s0, c.xy) + t7.Sample(s0, c.xy) + t8.Sample(s0, c.xy) + t9.Sample(s0, c.xy) + t10.Sample(s0, c.xyz);\n";

std::vector<uint8_t> ps(const std::string& extraDecl, const std::string& extraCode, const char* rows = "29") {
    std::string body = kPsBody;
    if (std::string(rows) != "29") body.replace(body.find("r[29]"), 5, std::string("r[") + rows + "]");
    const std::string src = std::string(kPsHead) + extraDecl + "float4 main(float4 p : SV_Position) : SV_Target {\n" + body +
                            extraCode + "return c; }\n";
    return test::stripLikeExe(test::compileHlsl(src.c_str(), "main", "ps_5_0"));
}

} // namespace

// ---- calls before init, and a failed init leaves nothing ----

C5R_TEST(calls_before_init_fail) {
    Renderer r;
    CHECK(!r.ready());
    shaders::ShaderSection sec;
    gamefs::GameFiles fs;
    CHECK_ERR(r.init(nullptr, sec, fs), Err::BadLayout);
    CHECK(!r.ready());
    CHECK_ERR(r.setMesh(Mesh()), Err::NotFound);
    FrameInputs in;
    CHECK_ERR(r.draw(nullptr, nullptr, nullptr, in, nullptr), Err::NotFound);
    CHECK(!r.hasMesh());
    CHECK_EQ(r.chunkCount(), 0u);
    CHECK(r.ownTexture(0) == nullptr);
    CHECK(r.frames().empty());
    uint32_t n = 0;
    const D3D11_INPUT_ELEMENT_DESC* e = iceInputLayout(&n);
    CHECK_EQ(n, 2u);
    // stock's layout [L draw 171]: TEXCOORD0 R16G16_SNORM @0, TEXCOORD1 R8G8B8A8_UNORM @4, slot 0, per vertex
    CHECK(std::string(e[0].SemanticName) == "TEXCOORD" && e[0].SemanticIndex == 0 && e[0].Format == DXGI_FORMAT_R16G16_SNORM &&
          e[0].AlignedByteOffset == 0 && e[0].InputSlot == 0 && e[0].InputSlotClass == D3D11_INPUT_PER_VERTEX_DATA);
    CHECK(std::string(e[1].SemanticName) == "TEXCOORD" && e[1].SemanticIndex == 1 && e[1].Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
          e[1].AlignedByteOffset == 4 && e[1].InputSlot == 0 && e[1].InputSlotClass == D3D11_INPUT_PER_VERTEX_DATA);
    CHECK(std::string(textureFile(0)) == "Ice_Environment_Diffuse.dds" && std::string(textureFile(7)).empty());
}

// ---- (b) the module against the capture ----

C5R_TEST(programs_textures_and_views_equal_the_capture) {
    Inputs& g = inputs();
    if (!g.ok) return;
    std::vector<uint8_t> vs, psb;
    if (!readRef(kVsFile, vs, 1248) || !readRef(kPsFile, psb, 8824)) return;
    CHECK(blobOf(g.sec, kIceVsVa) == vs);
    CHECK(blobOf(g.sec, kIcePsVa) == psb);
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    CHECK_OK(r.init(d.dev, g.sec, g.fs));
    CHECK(r.ready());
    // the install's textures, read back, equal the capture's bytes (asset set: VT; Ice_D.dds from VT, polar-ice.md 1 I8)
    for (uint32_t s = 0; s < 7; ++s) {
        std::vector<uint8_t> want, got;
        if (!readRef(kOwnFiles[s], want)) return;
        CHECK_OK(readTexture(d.dev, d.ctx, r.ownTexture(s), got));
        if (got != want)
            test::fail(__FILE__, __LINE__, "t" + std::to_string(s) + " (" + textureFile(s) + "): " + std::to_string(got.size()) +
                                               " bytes differ from the capture's " + std::to_string(want.size()));
    }
    // the views as stock's [L draw 171]
    const DXGI_FORMAT fmt[7] = {DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
                                DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
                                DXGI_FORMAT_A8_UNORM};
    const uint32_t mips[7] = {8, 10, 10, 10, 10, 10, 10};
    for (uint32_t s = 0; s < 7; ++s) {
        D3D11_SHADER_RESOURCE_VIEW_DESC v;
        r.ownTexture(s)->GetDesc(&v);
        CHECK_EQ(v.Format, fmt[s]);
        CHECK_EQ(v.ViewDimension, s == 0 ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURE2D);
        CHECK_EQ(s == 0 ? v.TextureCube.MipLevels : v.Texture2D.MipLevels, mips[s]);
    }
}

C5R_TEST(draw_with_the_capture_inputs_binds_stock_state_and_is_deterministic) {
    const uint32_t W = 1277, H = 706;   // the capture's 2554 x 1411 halved (the viewport scales; the matrices are the capture's)
    std::vector<uint8_t> a, b, scene;
    Run ra;
    if (!ra.go(W, H, a)) return;
    // one draw of the whole mesh, IceFrame = the capture's in every stored byte
    CHECK_EQ(ra.st.copies, 1u);
    CHECK_EQ(ra.st.draws, 1u);
    CHECK_EQ(ra.st.indices, static_cast<uint64_t>(kTinyTriangles) * 3);
    if (!readRef(kScene, scene, kIceFrameBytes)) return;
    CHECK_EQ(ra.r.frames().size(), static_cast<size_t>(1));
    uint32_t first = 0;
    CHECK_EQ(maskedDiff(ra.r.frames()[0].data(), scene.data(), &first), 0u);
    // the bound state equals draw 171's (state.json: om, rs, PS samp, ia)
    ID3D11DeviceContext* c = ra.d.ctx;
    {
        ID3D11BlendState* bs = nullptr;
        float f[4];
        UINT mask = 0;
        c->OMGetBlendState(&bs, f, &mask);
        CHECK(bs != nullptr);
        if (bs) {
            D3D11_BLEND_DESC d;
            bs->GetDesc(&d);
            const D3D11_RENDER_TARGET_BLEND_DESC& t = d.RenderTarget[0];
            CHECK(t.BlendEnable && t.SrcBlend == D3D11_BLEND_ONE && t.DestBlend == D3D11_BLEND_INV_SRC_ALPHA &&
                  t.BlendOp == D3D11_BLEND_OP_ADD && t.SrcBlendAlpha == D3D11_BLEND_ONE && t.DestBlendAlpha == D3D11_BLEND_ZERO &&
                  t.BlendOpAlpha == D3D11_BLEND_OP_ADD && t.RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL);
            CHECK(!d.AlphaToCoverageEnable && !d.IndependentBlendEnable);
            bs->Release();
        }
        ID3D11DepthStencilState* ds = nullptr;
        UINT ref = 0;
        c->OMGetDepthStencilState(&ds, &ref);
        CHECK(ds != nullptr);
        if (ds) {
            D3D11_DEPTH_STENCIL_DESC d;
            ds->GetDesc(&d);
            CHECK(d.DepthEnable && d.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL && d.DepthFunc == D3D11_COMPARISON_LESS_EQUAL &&
                  !d.StencilEnable);
            ds->Release();
        }
        CHECK_EQ(ref, 0u);
        ID3D11RasterizerState* rs = nullptr;
        c->RSGetState(&rs);
        CHECK(rs != nullptr);
        if (rs) {
            D3D11_RASTERIZER_DESC d;
            rs->GetDesc(&d);
            CHECK(d.FillMode == D3D11_FILL_SOLID && d.CullMode == D3D11_CULL_BACK && d.FrontCounterClockwise && d.DepthClipEnable &&
                  d.DepthBias == 0 && !d.ScissorEnable && !d.MultisampleEnable);
            rs->Release();
        }
        ID3D11SamplerState* ss[11] = {};
        c->PSGetSamplers(0, 11, ss);
        for (uint32_t i = 0; i < 11; ++i) {
            CHECK(ss[i] != nullptr);
            if (!ss[i]) continue;
            D3D11_SAMPLER_DESC d;
            ss[i]->GetDesc(&d);
            const D3D11_TEXTURE_ADDRESS_MODE am = i == 10 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
            CHECK(d.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && d.AddressU == am && d.AddressV == am && d.AddressW == am &&
                  d.MipLODBias == 0.0f && d.MinLOD == 0.0f && d.MaxLOD >= 3.4e38f && d.ComparisonFunc == D3D11_COMPARISON_NEVER);
            ss[i]->Release();
        }
        D3D11_PRIMITIVE_TOPOLOGY topo;
        c->IAGetPrimitiveTopology(&topo);
        CHECK_EQ(topo, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* ib = nullptr;
        DXGI_FORMAT ifmt = DXGI_FORMAT_UNKNOWN;
        UINT ioff = 7;
        c->IAGetIndexBuffer(&ib, &ifmt, &ioff);
        CHECK(ib && ifmt == DXGI_FORMAT_R16_UINT && ioff == 0);
        if (ib) {
            D3D11_BUFFER_DESC d;
            ib->GetDesc(&d);
            CHECK(d.ByteWidth == 580710 && d.Usage == D3D11_USAGE_IMMUTABLE);
            ib->Release();
        }
        ID3D11Buffer* vb = nullptr;
        UINT stride = 0, voff = 7;
        c->IAGetVertexBuffers(0, 1, &vb, &stride, &voff);
        CHECK(vb && stride == 8 && voff == 0);
        if (vb) {
            D3D11_BUFFER_DESC d;
            vb->GetDesc(&d);
            CHECK(d.ByteWidth == 489112 && d.Usage == D3D11_USAGE_IMMUTABLE);
            vb->Release();
        }
        ID3D11ShaderResourceView* srv[11] = {};
        c->PSGetShaderResources(0, 11, srv);
        for (uint32_t i = 0; i < 11; ++i) {
            ID3D11ShaderResourceView* want = i < 7 ? ra.r.ownTexture(i) : ra.sh.v[i - 7];
            CHECK(srv[i] == want);
            rel(srv[i]);
        }
        ID3D11Buffer* cb[2] = {};
        c->PSGetConstantBuffers(0, 2, cb);
        CHECK(cb[0] == nullptr && cb[1] != nullptr);
        if (cb[1]) {
            D3D11_BUFFER_DESC d;
            cb[1]->GetDesc(&d);
            CHECK_EQ(d.ByteWidth, kIceFrameBytes);
        }
        rel(cb[0]);
        rel(cb[1]);
        D3D11_VIEWPORT vp;
        UINT nvp = 1;
        c->RSGetViewports(&nvp, &vp);
        CHECK(nvp == 1 && vp.Width == static_cast<float>(W) && vp.Height == static_cast<float>(H) && vp.MaxDepth == 1.0f);
    }
    // the picture: ice drawn, the same on a second hardware device
    const uint32_t n = covered(a);
    std::printf("ice smoke draw: %u of %u pixels covered\n", n, W * H);
    CHECK(n > W * H / 100);
    Run rb;
    if (!rb.go(W, H, b)) return;
    CHECK(a == b);
    if (a != b) {
        uint32_t diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff += a[i] != b[i];
        test::fail(__FILE__, __LINE__, "the two devices differ in " + std::to_string(diff) + " bytes");
    }
    const std::string out = test::workDir() + "/ice_smoke_draw171.bmp";
    if (writeBmp(out, a, W, H)) std::printf("diagnostic picture: %s\n", out.c_str());
}

// The copies and the cull: wrap draws three copies; the cull box of draw 171's camera keeps copy 0's chunk only; a box
// away from the ice culls everything and draws nothing.
C5R_TEST(draw_copies_and_cull) {
    std::vector<uint8_t> a;
    Run r;
    if (!r.go(320, 180, a)) return;
    FrameInputs in = r.in;
    in.wrapX = true;
    FrameStats st;
    r.t.clear(r.d.ctx);
    CHECK_OK(r.r.draw(r.d.ctx, r.t.rtv, r.t.dsv, in, &st));
    CHECK_EQ(st.copies, 3u);
    CHECK_EQ(st.draws, 3u);
    CHECK_EQ(r.r.frames().size(), static_cast<size_t>(3));
    in.haveCullBox = true;
    const float box[4] = {500.0f, -1100.0f, 1700.0f, -700.0f};   // around the south-pole camera
    std::memcpy(in.cullBox, box, sizeof box);
    CHECK_OK(r.r.draw(r.d.ctx, r.t.rtv, r.t.dsv, in, &st));
    CHECK_EQ(st.draws, 1u);
    CHECK_EQ(st.chunksCulled, 2u);
    const float away[4] = {5000.0f, 5000.0f, 6000.0f, 6000.0f};
    std::memcpy(in.cullBox, away, sizeof away);
    in.wrapX = false;
    r.t.clear(r.d.ctx);
    CHECK_OK(r.r.draw(r.d.ctx, r.t.rtv, r.t.dsv, in, &st));
    CHECK_EQ(st.draws, 0u);
    CHECK_EQ(st.chunksCulled, 1u);
    std::vector<uint8_t> px;
    CHECK_OK(r.t.read(r.d.ctx, px));
    CHECK_EQ(covered(px), 0u);
}

// ---- (c) negatives ----

// init: a missing blob, a blob of another stage, a declaration, a slot or the IceFrame layout unlike stock's.
C5R_TEST(init_rejects_programs_unlike_stocks) {
    Inputs& g = inputs();
    if (!g.ok) return;
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    InitOptions o;
    o.vsVa = 0x00401234u;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::NotFound);   // VS not in the section
    CHECK(!r.ready());
    o = InitOptions();
    o.psVa = 0x00401234u;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::NotFound);   // PS not in the section
    o = InitOptions();
    o.vsVa = kIcePsVa;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::BadLayout);  // a PS given as the VS
    o = InitOptions();
    o.psVa = kIceVsVa;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::BadLayout);  // a VS given as the PS
    CHECK(!r.ready());
    CHECK_ERR(r.setMesh(Mesh()), Err::NotFound);

    // a synthetic section: the EXE's two programs and HLSL stand-ins that differ in one point each
    const std::vector<uint8_t> vsReal = blobOf(g.sec, kIceVsVa), psReal = blobOf(g.sec, kIcePsVa);
    CHECK(!vsReal.empty() && !psReal.empty());
    const std::vector<uint8_t> vsPosition = test::stripLikeExe(test::compileHlsl(
        "cbuffer F : register(b1) { float4 r[14]; };\n"
        "float4 main(float4 p : POSITION) : SV_Position { return p * r[13]; }\n", "main", "vs_5_0"));
    const std::vector<uint8_t> vsCb0 = test::stripLikeExe(test::compileHlsl(
        "cbuffer O : register(b0) { float4 o; }; cbuffer F : register(b1) { float4 r[14]; };\n"
        "float4 main(float2 a : TEXCOORD0, float4 b : TEXCOORD1) : SV_Position { return b * r[13] + a.xyxy * o; }\n",
        "main", "vs_5_0"));
    const std::vector<uint8_t> psGood = ps("", "");
    const std::vector<uint8_t> psT3 = ps("Texture2D t3 : register(t3);\n", "c += t3.Sample(s0, c.xy);\n");
    const std::vector<uint8_t> psT10as2d = [] {
        std::string head = kPsHead;
        head.replace(head.find("Texture3D t10"), 13, "Texture2D t10");
        std::string body = kPsBody;
        body.replace(body.find("t10.Sample(s0, c.xyz)"), 21, "t10.Sample(s0, c.xy)");
        const std::string src = head + "float4 main(float4 p : SV_Position) : SV_Target {\n" + body + "return c; }\n";
        return test::stripLikeExe(test::compileHlsl(src.c_str(), "main", "ps_5_0"));
    }();
    const std::vector<uint8_t> psNoT9 = [] {
        std::string body = kPsBody;
        body.replace(body.find(" + t9.Sample(s0, c.xy)"), 22, "");
        const std::string src = std::string(kPsHead) + "float4 main(float4 p : SV_Position) : SV_Target {\n" + body + "return c; }\n";
        return test::stripLikeExe(test::compileHlsl(src.c_str(), "main", "ps_5_0"));
    }();
    const std::vector<uint8_t> psRows20 = ps("", "", "19");
    const std::vector<uint8_t> psS11 = ps("SamplerState s11 : register(s11);\n", "c += t1.Sample(s11, c.yx);\n");
    for (const auto* b : {&vsPosition, &vsCb0, &psGood, &psT3, &psT10as2d, &psNoT9, &psRows20, &psS11}) CHECK(!b->empty());
    std::vector<uint32_t> va;
    shaders::ShaderSection syn;
    CHECK_OK(syn.parse(sectionWith({vsReal, psReal, vsPosition, vsCb0, psGood, psT3, psT10as2d, psNoT9, psRows20, psS11}, va)));
    if (va.size() != 10) return;
    auto tryInit = [&](uint32_t vs, uint32_t psVa) {
        InitOptions io;
        io.vsVa = vs;
        io.psVa = psVa;
        return r.init(d.dev, syn, g.fs, io);
    };
    CHECK_OK(tryInit(va[0], va[1]));                             // the EXE's own programs from the synthetic section
    CHECK(r.ready());
    CHECK_ERR(tryInit(va[2], va[1]), Err::BadLayout);            // VS input POSITION, not TEXCOORD0/1
    CHECK(!r.ready());                                           // the earlier good init is gone, nothing half-made
    CHECK_ERR(tryInit(va[3], va[1]), Err::BadLayout);            // VS declares cb0 too
    CHECK_OK(tryInit(va[0], va[4]));                             // a stand-in PS declared exactly as stock's passes
    CHECK_ERR(tryInit(va[0], va[5]), Err::BadLayout);            // t3 declared
    CHECK_ERR(tryInit(va[0], va[6]), Err::BadLayout);            // t10 a texture2d
    CHECK_ERR(tryInit(va[0], va[7]), Err::BadLayout);            // t9 missing
    CHECK_ERR(tryInit(va[0], va[8]), Err::BadLayout);            // cb1 of 20 rows: not the IceFrame layout
    CHECK_ERR(tryInit(va[0], va[9]), Err::BadLayout);            // sampler s11
    CHECK(!r.ready());
}

// init: each of the five texture files missing from an install fails NotFound naming it.
C5R_TEST(init_names_a_missing_texture_file) {
    Inputs& g = inputs();
    if (!g.ok) return;
    Device d;
    CHECK_OK(d.create());
    const char* files[5] = {"Ice_Environment_Diffuse.dds", "Ice_D.dds", "Ice_Bump.lean", "Ice_D_Sides.dds", "Ice_Bump_Sides.lean"};
    std::vector<std::vector<uint8_t>> bytes(5);
    for (int i = 0; i < 5; ++i) CHECK_OK(g.fs.read(files[i], bytes[i]));
    const std::string root = test::workDir() + "/ice_fake_install";
    for (int missing = 0; missing < 5; ++missing) {
        const std::string inst = root + "/m" + std::to_string(missing);
        CreateDirectoryW(wide(root).c_str(), nullptr);
        CreateDirectoryW(wide(inst).c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Assets").c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Resource").c_str(), nullptr);
        for (int i = 0; i < 5; ++i) {
            const std::string p = inst + "/Assets/" + files[i];
            if (i == missing) DeleteFileW(wide(p).c_str());
            else CHECK(writeFile(p, bytes[i]));
        }
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        const Status st = r.init(d.dev, g.sec, fs);
        CHECK_ERR(st, Err::NotFound);
        CHECK(st.msg.find(files[missing]) != std::string::npos);
        CHECK(!r.ready());
        if (missing == 4) {   // every file present: the same install inits
            CHECK(writeFile(inst + "/Assets/" + files[4], bytes[4]));
            gamefs::GameFiles all;
            CHECK_OK(all.open(wide(inst)));
            CHECK_OK(r.init(d.dev, g.sec, all));
            CHECK(r.ready());
        }
    }
    // a damaged texture: the parser's reason, nothing made
    {
        const std::string inst = root + "/bad";
        CreateDirectoryW(wide(inst).c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Assets").c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Resource").c_str(), nullptr);
        for (int i = 0; i < 5; ++i) {
            std::vector<uint8_t> b = bytes[i];
            if (i == 3) b.resize(100);   // Ice_D_Sides.dds cut inside the header
            CHECK(writeFile(inst + "/Assets/" + files[i], b));
        }
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        const Status st = r.init(d.dev, g.sec, fs);
        CHECK_ERR(st, Err::Truncated);
        CHECK(st.msg.find("Ice_D_Sides.dds") != std::string::npos);
        CHECK(!r.ready());
    }
}

// setMesh: every check, and a failed call keeps the previous mesh (or none).
C5R_TEST(setmesh_rejects_bad_meshes_and_keeps_the_old_one) {
    Inputs& g = inputs();
    if (!g.ok) return;
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    CHECK_OK(r.init(d.dev, g.sec, g.fs));
    Mesh good;
    if (!capturedMesh(good)) return;
    auto bad = [&](const Mesh& m, Err want) {
        CHECK_ERR(r.setMesh(m), want);
    };
    Mesh m = good;
    m.fields.valid = false;
    bad(m, Err::NotFound);
    CHECK(!r.hasMesh());   // nothing half-made on a fresh renderer
    m = good;
    m.fields.set(field::kSubsurface + 4, NAN);
    bad(m, Err::BadLayout);
    m = good;
    m.fields.set(field::kBox, 5000.0f);   // min x > max x
    bad(m, Err::BadLayout);
    m = good;
    m.vb.resize(m.vb.size() - 3);         // not whole vertices
    bad(m, Err::BadLayout);
    m = good;
    m.vb.clear();
    bad(m, Err::BadLayout);
    m = good;
    m.ib.resize(m.ib.size() - 1);         // not whole R16 indices
    bad(m, Err::BadLayout);
    m = good;
    m.ib.clear();
    bad(m, Err::BadLayout);
    m = good;
    m.chunks.clear();                     // zero chunks
    bad(m, Err::BadLayout);
    CHECK(!r.hasMesh());
    m = good;
    m.chunks[0].triangles = 0;
    bad(m, Err::BadLayout);
    m = good;
    m.chunks[0].startIndex = 3;           // the chunk runs 3 indices past the IB
    bad(m, Err::BadLayout);
    m = good;
    m.chunks[0].triangles += 1;           // the chunk past the buffer
    bad(m, Err::BadLayout);
    m = good;
    m.vb.resize(m.vb.size() / 2);         // the VB's size disagrees with the chunk's indices
    bad(m, Err::BadLayout);
    m = good;
    m.chunks[0].baseVertex = 1;           // index max + 1 past the VB
    bad(m, Err::BadLayout);
    m = good;
    m.chunks[0].baseVertex = -1;
    bad(m, Err::BadLayout);
    m = good;
    m.chunks[0].box[2] = NAN;
    bad(m, Err::BadLayout);
    CHECK(!r.hasMesh());
    CHECK_EQ(r.chunkCount(), 0u);
    // the good mesh, then a bad one: the good one stays
    CHECK_OK(r.setMesh(good));
    CHECK(r.hasMesh());
    m = good;
    m.chunks.push_back(m.chunks[0]);
    m.chunks[1].startIndex = 1;           // second chunk past the IB
    bad(m, Err::BadLayout);
    CHECK(r.hasMesh());
    CHECK_EQ(r.chunkCount(), 1u);
    // a mesh of two chunks over the same buffers is accepted (huge has 8)
    m = good;
    m.chunks[0].triangles = 1000;
    m.chunks.push_back(good.chunks[0]);
    m.chunks[1].startIndex = 3000;
    m.chunks[1].triangles = kTinyTriangles - 1000;
    CHECK_OK(r.setMesh(m));
    CHECK_EQ(r.chunkCount(), 2u);
    r.clearMesh();
    CHECK(!r.hasMesh());
}

// draw: every check; a failed draw draws nothing.
C5R_TEST(draw_rejects_missing_inputs_and_draws_nothing) {
    std::vector<uint8_t> a;
    Run r;
    if (!r.go(160, 90, a)) return;
    const FrameInputs good = r.in;
    std::vector<uint8_t> px;
    auto failsClean = [&](const FrameInputs& in, Err want, const char* name) {
        r.t.clear(r.d.ctx);
        const Status st = r.r.draw(r.d.ctx, r.t.rtv, r.t.dsv, in, nullptr);
        CHECK_ERR(st, want);
        if (name) CHECK(st.msg.find(name) != std::string::npos);
        CHECK_OK(r.t.read(r.d.ctx, px));
        CHECK_EQ(covered(px), 0u);
    };
    FrameInputs in = good;
    in.fogRamp = nullptr;
    failsClean(in, Err::NotFound, "t7");
    in = good;
    in.fowMask = nullptr;
    failsClean(in, Err::NotFound, "t8");
    in = good;
    in.cloudBase = nullptr;
    failsClean(in, Err::NotFound, "t9");
    in = good;
    in.fowLut = nullptr;
    failsClean(in, Err::NotFound, "t10");
    in = good;
    in.cam.width = 0;
    failsClean(in, Err::BadLayout, nullptr);
    in = good;
    in.cam.view[3] = NAN;
    failsClean(in, Err::BadLayout, nullptr);
    in = good;
    in.wrapX = true;
    in.worldWidth = 0;   // copies 1, 2 need W: no copy is drawn, copy 0 included
    failsClean(in, Err::BadLayout, nullptr);
    in = good;
    in.haveCullBox = true;
    in.cullBox[1] = NAN;
    failsClean(in, Err::BadLayout, nullptr);
    CHECK_ERR(r.r.draw(nullptr, r.t.rtv, r.t.dsv, good, nullptr), Err::BadLayout);
    CHECK_ERR(r.r.draw(r.d.ctx, nullptr, r.t.dsv, good, nullptr), Err::BadLayout);
    r.r.clearMesh();
    failsClean(good, Err::NotFound, "mesh");
}

C5R_TEST_MAIN
