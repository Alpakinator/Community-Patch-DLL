// c5r::forest draw tests (ticket 4551, epic 0431; research/05-graphics-composition/trees.md 11). D137: offline, no game
// client. (a) the constants, the CRT rand, the card choice and buffers; (b) the module against the 3907 stock captures:
// programs, textures and card buffers equal the capture's, cb1 equal to every captured block in every stored byte, and
// every ForestShader draw of units_land/L01 drawn by the module equal to a plain replay of the captured draw, then the
// whole forest stage of the frame; (c) every check of init, replaceCards, computeForestFrame and draw, each asserting the
// status and that nothing half-made stays. Own code; the EXE, the install and the captures are read at run time.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "c5r/forest/draw.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrain/terrain.h"
#include "draw_util.h"
#include "testkit.h"
#include "testutil.h"

using namespace c5r;
using namespace c5r::forest;
using namespace c5r::ftest;

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

const char* const kL01 = "units_land/L01";
const uint32_t kAsia = 2;   // every forest plot of the 3907 captures is style 2 (trees.md 11)

std::wstring exeOrSkip() {
    const std::string exe = test::exePath();
    if (exe.empty()) test::skip("C5R_EXE not set: the ForestShader programs come from the EXE");
    return wide(exe);
}
std::wstring gameOrSkip() {
    const std::string g = test::gameDir();
    if (g.empty()) test::skip("C5R_GAME_DIR not set: the cards and textures come from the install");
    return wide(g);
}

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

std::vector<uint8_t> blobOf(const shaders::ShaderSection& sec, uint32_t va) {
    const shaders::ShaderBlob* b = sec.findByVa(va);
    return b ? std::vector<uint8_t>(b->bytes, b->bytes + b->size) : std::vector<uint8_t>();
}

// A section holding these blobs (8-aligned); their VAs in vas (as the ice tests).
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

// The kind of a captured draw: its VB size (29,160 B forest, 32,400 B jungle in the 3907 captures; trees.md 2.4).
uint32_t kindOf(const DrawRef& d) { return d.vbBytes == 29160 ? kForest : kJungle; }

uint32_t covered(const std::vector<uint8_t>& rgba, const uint8_t clear[4]) {
    uint32_t n = 0;
    for (size_t i = 0; i < rgba.size(); i += 4)
        if (std::memcmp(&rgba[i], clear, 4) != 0) ++n;
    return n;
}

// The captured cb1 blocks of an export, one per view, in view order.
bool capturedBlocks(const ExportRef& ex, std::vector<std::vector<uint8_t>>& out) {
    out.assign(ex.views(), std::vector<uint8_t>());
    for (const DrawRef& d : ex.draws)
        if (out[d.view].empty() && !readRef(ex.name, d.cb1, out[d.view], kFrameBytes)) return false;
    return true;
}

// HLSL stand-ins with one difference each from the EXE's ForestShader programs (compiled at test time, stripped like
// the EXE's).
const char* const kVsGood =
    "cbuffer F : register(b1) { float4 r[9]; }; cbuffer B : register(b7) { float4 t[90]; };\n"
    "float4 main(float3 p : POSITION, float3 n : NORMAL, float2 uv : TEXCOORD0, uint4 bi : BLENDINDICES) : SV_Position {\n"
    "  return float4(p * t[bi.x].w + t[bi.x].xyz, 1) * r[0] + float4(n, uv.x) * r[8]; }\n";
const char* const kPsHead =
    "cbuffer F : register(b1) { float4 r[15]; };\n"
    "Texture2D t0 : register(t0); Texture2D t1 : register(t1); Texture2D t3 : register(t3); Texture2D t4 : register(t4);\n"
    "Texture3D t5 : register(t5); Texture2D t6 : register(t6); Texture2D t7 : register(t7); Texture3D t8 : register(t8);\n"
    "SamplerState s0 : register(s0);\n";
const char* const kPsBody =
    "float4 c = r[14] + r[0];\n"
    "c += t0.Sample(s0, c.xy) + t1.Sample(s0, c.xy) + t3.Sample(s0, c.xy) + t4.Sample(s0, c.xy) + t5.Sample(s0, c.xyz);\n"
    "c += t6.Sample(s0, c.xy) + t7.Sample(s0, c.xy) + t8.Sample(s0, c.xyz);\n";

std::vector<uint8_t> ps(const std::string& extraDecl, const std::string& extraCode, const char* row = "14",
                        const char* head = kPsHead, const char* body = kPsBody) {
    std::string b = body;
    if (std::string(row) != "14") b.replace(b.find("r[14]"), 5, std::string("r[") + row + "]");
    const std::string src = std::string(head) + extraDecl + "float4 main(float4 p : SV_Position) : SV_Target {\n" + b + extraCode +
                            "return c; }\n";
    return test::stripLikeExe(test::compileHlsl(src.c_str(), "main", "ps_5_0"));
}

const uint8_t kClear8[4] = {84, 84, 84, 0};   // 0.33 x 255 rounded, the scene clear of the captures (0.33, 0.33, 0.33, 0)
const float kClear[4] = {0.33f, 0.33f, 0.33f, 0.0f};

} // namespace

// ---- (a) constants, rand, cards ----

C5R_TEST(constants_rand_and_names) {
    // the MSVC CRT's first values after srand(1) [the CRT's documented LCG]
    CrtRand r(1);
    const int want[5] = {41, 18467, 6334, 26500, 19169};
    for (int w : want) CHECK_EQ(r.next(), w);
    CrtRand r2(12345);
    uint32_t s = 12345;
    for (int i = 0; i < 100; ++i) {
        s = s * 214013u + 2531011u;
        CHECK_EQ(r2.next(), static_cast<int>((s >> 16) & 0x7FFF));
    }
    // the ColorKey index map {0, 3, 2, 4, 1} and out of range
    const uint32_t lut[5] = {0, 3, 2, 4, 1};
    for (uint32_t e = 0; e < 5; ++e) CHECK_EQ(lutIndex(e), lut[e]);
    CHECK_EQ(lutIndex(5), kLuts);
    CHECK(std::string(modelFile(0, kForest)) == "forest_europe_2.gr2");
    CHECK(std::string(modelFile(4, kJungle)) == "jungle_europe_2.gr2");
    CHECK(std::string(diffuseFile(2, kJungle)) == "jungle_asia_2.dds");
    CHECK(std::string(overlayFile(3, kForest)) == "forest_overlay_africa_2.dds");
    CHECK(std::string(modelFile(5, kForest)).empty() && std::string(diffuseFile(0, 2)).empty());
    // the card choice: forest then jungle per slot, per entry, with each entry's counts
    CrtRand a(7), b(7);
    const uint32_t nf[5] = {12, 12, 12, 12, 12}, nj[5] = {17, 17, 17, 17, 17};
    uint32_t fc[kEntries][kSlots], jc[kEntries][kSlots];
    chooseCards(a, nf, nj, fc, jc);
    for (uint32_t e = 0; e < kEntries; ++e)
        for (uint32_t k = 0; k < kSlots; ++k) {
            CHECK_EQ(fc[e][k], static_cast<uint32_t>(b.next()) % 12u);
            CHECK_EQ(jc[e][k], static_cast<uint32_t>(b.next()) % 17u);
        }
    CHECK_EQ(a.state(), b.state());   // 900 calls
    // the layout stock's draws show [L]
    uint32_t n = 0;
    const D3D11_INPUT_ELEMENT_DESC* l = forestInputLayout(&n);
    CHECK_EQ(n, 4u);
    CHECK(std::string(l[0].SemanticName) == "POSITION" && l[0].Format == DXGI_FORMAT_R32G32B32_FLOAT && l[0].AlignedByteOffset == 0);
    CHECK(std::string(l[1].SemanticName) == "NORMAL" && l[1].Format == DXGI_FORMAT_R32G32B32_FLOAT && l[1].AlignedByteOffset == 12);
    CHECK(std::string(l[2].SemanticName) == "TEXCOORD" && l[2].Format == DXGI_FORMAT_R32G32_FLOAT && l[2].AlignedByteOffset == 24);
    CHECK(std::string(l[3].SemanticName) == "BLENDINDICES" && l[3].Format == DXGI_FORMAT_R8G8B8A8_UINT && l[3].AlignedByteOffset == 32);
    // the stored bytes of cb1: 240 - 28 never stored (9.zw, 10.yzw, 11.w, 12.w)
    uint32_t stored = 0;
    for (uint32_t i = 0; i < kFrameBytes; ++i) stored += forestFrameStored(i) ? 1u : 0u;
    CHECK_EQ(stored, 212u);
    CHECK(!forestFrameStored(0x98) && !forestFrameStored(0xA4) && !forestFrameStored(0xBC) && !forestFrameStored(0xCC));
    CHECK(forestFrameStored(0xE8) && forestFrameStored(0x94) && forestFrameStored(0xEC));
}

C5R_TEST(card_buffers_layout_and_refusals) {
    // two synthetic cards: 3 vertices / 1 triangle and 4 vertices / 2 triangles
    std::vector<Card> cards(2);
    for (uint32_t c = 0; c < 2; ++c) {
        const uint32_t nv = 3 + c;
        for (uint32_t v = 0; v < nv; ++v)
            for (uint32_t i = 0; i < 8; ++i) cards[c].vertices.push_back(static_cast<float>(c * 100 + v * 10 + i));
        cards[c].indices = c == 0 ? std::vector<uint32_t>{0, 1, 2} : std::vector<uint32_t>{0, 1, 2, 2, 3, 0};
    }
    uint32_t choice[kSlots];
    for (uint32_t k = 0; k < kSlots; ++k) choice[k] = k % 2;
    CardBuffers b;
    CHECK_OK(buildCardBuffers(cards, choice, b));
    CHECK_EQ(b.vb.size(), static_cast<size_t>(4 * 90 * 36));   // largest card x 90
    CHECK_EQ(b.ib.size(), static_cast<size_t>(6 * 90 * 2));
    CHECK_EQ(b.indexCount, 45u * 3u + 45u * 6u);
    // slot 1 (card 1) starts at vertex 3; its vertex 2 holds card 1's floats and BLENDINDICES (1, 0, 0, 0)
    float f[8];
    std::memcpy(f, b.vb.data() + (3 + 2) * 36, 32);
    CHECK(f[0] == 120.0f && f[7] == 127.0f);
    CHECK(b.vb[(3 + 2) * 36 + 32] == 1 && b.vb[(3 + 2) * 36 + 33] == 0 && b.vb[(3 + 2) * 36 + 35] == 0);
    uint16_t i3;
    std::memcpy(&i3, b.ib.data() + 3 * 2, 2);   // slot 1's first index = 0 + base 3
    CHECK_EQ(i3, 3);
    // the tail past the 315 used vertices is zero
    bool tailZero = true;
    for (size_t i = static_cast<size_t>(45 * 3 + 45 * 4) * 36; i < b.vb.size(); ++i) tailZero = tailZero && b.vb[i] == 0;
    CHECK(tailZero);
    // refusals leave `out` unchanged
    const CardBuffers keep = b;
    CHECK_ERR(buildCardBuffers(std::vector<Card>(), choice, b), Err::BadLayout);
    choice[7] = 2;
    CHECK_ERR(buildCardBuffers(cards, choice, b), Err::BadLayout);
    choice[7] = 1;
    std::vector<Card> bad = cards;
    bad[1].indices.clear();
    CHECK_ERR(buildCardBuffers(bad, choice, b), Err::BadLayout);
    bad = cards;
    bad[0].vertices.clear();
    CHECK_ERR(buildCardBuffers(bad, choice, b), Err::BadLayout);
    bad = cards;
    bad[0].vertices.resize(800 * 8, 0.0f);   // 800 x 90 > 65,536 vertices
    CHECK_ERR(buildCardBuffers(bad, choice, b), Err::BadLayout);
    CHECK(b.vb == keep.vb && b.ib == keep.ib && b.indexCount == keep.indexCount);
    // cardsFromGr2 on bytes that are not a Granny file, and on a cut file
    std::vector<Card> out(1);
    const uint8_t junk[64] = {1, 2, 3};
    const Status s1 = cardsFromGr2(junk, sizeof junk, "junk.gr2", out);
    CHECK(!s1.ok() && s1.msg.find("junk.gr2") != std::string::npos);
    CHECK(out.empty());
    Inputs& g = inputs();
    if (!g.ok) return;
    std::vector<uint8_t> gr2;
    CHECK_OK(g.fs.read("forest_asia_2.gr2", gr2));
    std::vector<uint8_t> cut(gr2.begin(), gr2.begin() + static_cast<std::ptrdiff_t>(gr2.size() / 2));
    const Status s2 = cardsFromGr2(cut.data(), cut.size(), "forest_asia_2.gr2", out);
    CHECK(!s2.ok() && s2.msg.find("forest_asia_2.gr2") != std::string::npos);
    CHECK(out.empty());
    CHECK_OK(cardsFromGr2(gr2.data(), gr2.size(), "forest_asia_2.gr2", out));
    CHECK_EQ(out.size(), static_cast<size_t>(12));   // trees.md 2.1: 12 cards Plane01..12, 9 vertices, 8 triangles
    for (const Card& c : out) CHECK(c.vertexCount() == 9 && c.indices.size() == 24);
}

// ---- (b) the module against the captures ----

C5R_TEST(programs_textures_and_cards_equal_the_capture) {
    Inputs& g = inputs();
    if (!g.ok) return;
    const ExportRef* ex = findExport(kL01);
    if (!ex) return;
    std::vector<uint8_t> vs, psb;
    if (!readRef(ex->name, ex->vsFile, vs, 1100) || !readRef(ex->name, ex->psFile, psb, 2264)) return;
    CHECK(blobOf(g.sec, kForestVsVa) == vs);
    CHECK(blobOf(g.sec, kForestPsVa) == psb);
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    CHECK_OK(r.init(d.dev, g.sec, g.fs));
    CHECK(r.ready());
    // t0 / t1 of the Asia entry, both kinds, equal the capture's bytes (asset set: VT; trees.md 2.1)
    bool seen[2] = {false, false};
    for (const DrawRef& dr : ex->draws) {
        const uint32_t k = kindOf(dr);
        if (seen[k]) continue;
        seen[k] = true;
        for (uint32_t s = 0; s < 2; ++s) {
            std::vector<uint8_t> want, got;
            if (!readRef(ex->name, dr.t[s].file, want)) return;
            CHECK_OK(readTexture(d.dev, d.ctx, r.texture(kAsia, k, s), got));
            if (got != want)
                test::fail(__FILE__, __LINE__, std::string(s ? overlayFile(kAsia, k) : diffuseFile(kAsia, k)) + ": " +
                                                   std::to_string(got.size()) + " bytes differ from the capture's " +
                                                   std::to_string(want.size()));
            D3D11_SHADER_RESOURCE_VIEW_DESC v;
            r.texture(kAsia, k, s)->GetDesc(&v);
            CHECK_EQ(static_cast<uint32_t>(v.Format), dr.t[s].format);
            CHECK_EQ(v.Texture2D.MipLevels, dr.t[s].mips);
        }
        // the cards: every captured slot's vertices are one of the module's cards, in the card's own vertex order; the
        // buffers built from those choices equal the capture's in every byte the draw reads (the 33 first bytes of each
        // used vertex, the indices)
        std::vector<uint8_t> vb, ib;
        if (!readRef(ex->name, dr.vb, vb, dr.vbBytes) || !readRef(ex->name, dr.ib, ib, dr.ibBytes)) return;
        const std::vector<Card>& cards = r.cards(kAsia, k);
        CHECK_EQ(cards.size(), static_cast<size_t>(k == kForest ? 12 : 17));
        uint32_t choice[kSlots];
        size_t at = 0;
        bool found = true;
        for (uint32_t slot = 0; slot < kSlots && found; ++slot) {
            found = false;
            for (uint32_t c = 0; c < cards.size() && !found; ++c) {
                const Card& cd = cards[c];
                if ((at + cd.vertexCount()) * kStride > vb.size()) continue;
                bool eq = true;
                for (uint32_t v = 0; v < cd.vertexCount() && eq; ++v)
                    eq = std::memcmp(vb.data() + (at + v) * kStride, &cd.vertices[static_cast<size_t>(v) * 8], 32) == 0 &&
                         vb[(at + v) * kStride + 32] == slot;
                if (eq) {
                    choice[slot] = c;
                    at += cd.vertexCount();
                    found = true;
                }
            }
            if (!found) test::fail(__FILE__, __LINE__, std::string(modelFile(kAsia, k)) + ": slot " + std::to_string(slot) +
                                                           " of the captured VB is none of the module's cards");
        }
        if (!found) continue;
        CardBuffers b;
        CHECK_OK(buildCardBuffers(cards, choice, b));
        CHECK_EQ(b.vb.size(), vb.size());
        CHECK_EQ(b.ib.size(), ib.size());
        CHECK_EQ(b.indexCount, dr.indexCount);
        uint32_t vbDiff = 0;
        for (size_t v = 0; v < at; ++v) vbDiff += std::memcmp(b.vb.data() + v * kStride, vb.data() + v * kStride, 33) != 0;
        CHECK_EQ(vbDiff, 0u);
        CHECK(std::memcmp(b.ib.data(), ib.data(), static_cast<size_t>(dr.indexCount) * 2) == 0);
        std::printf("%s: %zu used vertices of %zu, the 90 cards found among the module's %zu\n", modelFile(kAsia, k), at,
                    vb.size() / kStride, cards.size());
    }
    CHECK(seen[0] && seen[1]);
    // the module's own buffers: 90 chosen cards per entry and kind, sized as stock's
    for (uint32_t e = 0; e < kEntries; ++e)
        for (uint32_t k = 0; k < kKinds; ++k) {
            CHECK_EQ(r.choice(e, k).size(), static_cast<size_t>(kSlots));
            CHECK_EQ(r.buffers(e, k).indexCount, 2160u);
            CHECK(r.texture(e, k, 0) && r.texture(e, k, 1));
        }
    CHECK_EQ(r.buffers(kAsia, kForest).vb.size(), static_cast<size_t>(29160));
    CHECK_EQ(r.buffers(kAsia, kJungle).vb.size(), static_cast<size_t>(32400));
}

// cb1 against every captured block (single view L01 and S04, two views c_ancient and p4_shown_all)
C5R_TEST(cb1_equals_the_captured_blocks) {
    if (manifest().empty()) return;
    uint32_t blocks = 0, maxUlp = 0;
    for (const ExportRef& ex : manifest()) {
        std::vector<std::vector<uint8_t>> cap;
        if (!capturedBlocks(ex, cap)) return;
        const FrameInputs in = capturedInputs(cap);
        for (uint32_t v = 0; v < cap.size(); ++v) {
            uint8_t out[kFrameBytes];
            CHECK_OK(computeForestFrame(in, v, out));
            uint32_t first = 0, ulp = 0;
            const uint32_t n = maskedDiff(out, cap[v].data(), 1, &first, &ulp);
            if (n) test::fail(__FILE__, __LINE__, ex.name + " view " + std::to_string(v) + ": " + std::to_string(n) +
                                                      " stored bytes differ, first at +0x" + std::to_string(first));
            maxUlp = std::max(maxUlp, ulp);
            ++blocks;
            // the never-stored bytes are 0 in the module's block
            for (uint32_t i = 0; i < kFrameBytes; ++i)
                if (!forestFrameStored(i)) CHECK_EQ(out[i], 0);
        }
    }
    std::printf("cb1: %u captured blocks equal in every stored byte; row 14.y within %u ulp\n", blocks, maxUlp);
    CHECK(blocks >= 6);
    CHECK(maxUlp <= 1);
    // the wrap view: view 1's matrix and camera of c_ancient are view 0's moved by the wrap period [L]
    const ExportRef* ca = findExport("cities/c_ancient");
    if (!ca) return;
    std::vector<std::vector<uint8_t>> cap;
    if (!capturedBlocks(*ca, cap)) return;
    CHECK_EQ(cap.size(), static_cast<size_t>(2));
    if (cap.size() == 2) {
        const float dx = f32(cap[0], 0xC0) - f32(cap[1], 0xC0);
        CHECK(std::fabs(dx - 3583.0f) < 0.01f);
    }
}

C5R_TEST(cb1_refusals) {
    const ExportRef* ex = findExport(kL01);
    if (!ex) return;
    std::vector<std::vector<uint8_t>> cap;
    if (!capturedBlocks(*ex, cap)) return;
    const FrameInputs good = capturedInputs(cap);
    uint8_t out[kFrameBytes];
    CHECK_OK(computeForestFrame(good, 0, out));
    CHECK_ERR(computeForestFrame(good, 0, nullptr), Err::BadLayout);
    CHECK_ERR(computeForestFrame(good, 1, out), Err::BadLayout);   // view past numViews
    FrameInputs in = good;
    in.numViews = 0;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in.numViews = 5;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.width = 0;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.views[0].view[5] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.proj[0] = INFINITY;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.views[0].overlayProj[12] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.fowAnim[2] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.fog.color[1] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.fog.density = 0;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.overlayAlpha = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.auxDir[2] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.views[0].camPos[1] = NAN;
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    in = good;
    in.views[0].haveCamPos = false;
    std::memset(in.views[0].view, 0, sizeof in.views[0].view);   // singular: no camera from the inverse
    CHECK_ERR(computeForestFrame(in, 0, out), Err::BadLayout);
    // without the camera position the inverse gives it (a real view matrix: the captured product is not one, so only
    // finiteness and the matrix rows are checked)
    in = good;
    in.views[0].haveCamPos = false;
    CHECK_OK(computeForestFrame(in, 0, out));
    CHECK(std::memcmp(out, cap[0].data(), 64) == 0);
}

namespace {

// Draws the capture's draws [first, first + count) of L01 through the module into `t` (stage = all of them in one
// target, else each into a cleared target and read back into pics) and through the replay into `u`.
struct PerDraw {
    Device d;
    Renderer r;
    Replay rep;
    const ExportRef* ex = nullptr;
    bool setUp(const char* exportName = kL01) {
        Inputs& g = inputs();
        if (!g.ok) return false;
        ex = findExport(exportName);
        if (!ex) return false;
        CHECK_OK(d.create());
        if (!d.dev) return false;
        const Status s = r.init(d.dev, g.sec, g.fs);
        CHECK_OK(s);
        if (!s.ok()) return false;
        return rep.init(d.dev, *ex);
    }
    // the module with the draw's own VB / IB, cb7 and t3-t8 of the capture, cb1 computed from the captured block
    bool moduleDraw(const Target& t, const DrawRef& dr, const Shared& sh, FrameStats* st) {
        std::vector<uint8_t> vb, ib, cb1, cb7;
        if (!readRef(ex->name, dr.vb, vb, dr.vbBytes) || !readRef(ex->name, dr.ib, ib, dr.ibBytes) ||
            !readRef(ex->name, dr.cb1, cb1, kFrameBytes) || !readRef(ex->name, dr.cb7, cb7, kBlockBytes))
            return false;
        const uint32_t k = kindOf(dr);
        Status s = r.replaceCards(kAsia, k, vb, ib, dr.indexCount);
        CHECK_OK(s);
        FrameInputs in = capturedInputs({cb1});
        sh.into(in, kAsia);
        Block b;
        b.entry = kAsia;
        b.kind = k;
        std::memcpy(b.trees, cb7.data(), kBlockBytes);
        s = r.draw(d.ctx, t.rtv, t.dsv, in, &b, 1, st);
        CHECK_OK(s);
        return s.ok();
    }
};

} // namespace

// Every ForestShader draw of L01 (46) through the module against a plain replay of the captured draw, each into a
// cleared target, and the whole stage of the frame (the 46 in order into one target); the numbers go to
// C5R_WORK/forest_perdraw.csv (render/evidence/0431/4551/ keeps a copy).
namespace {

struct ExportResult {
    uint32_t draws = 0, drawsWithPixels = 0, worst = 0;
    uint64_t drawn = 0;
    DiffStats stage;
    std::vector<uint8_t> stageModule, stageDiff;
};

// Every tree draw of p.ex through the module and through the replay, each into a cleared target, then the whole stage
// (all of them in the frame's order into one target each side); CSV lines into csv.
bool runExport(PerDraw& p, Target& tm, Target& tr, const Shared& sh, std::string& csv, ExportResult& res) {
    std::vector<uint8_t> a, b;
    for (const DrawRef& dr : p.ex->draws) {
        tm.clear(p.d.ctx, kClear);
        tr.clear(p.d.ctx, kClear);
        FrameStats st;
        if (!p.moduleDraw(tm, dr, sh, &st)) return false;
        CHECK_EQ(st.draws, 1u);
        CHECK_EQ(st.indices, static_cast<uint64_t>(dr.indexCount));
        if (!p.rep.draw(p.d.ctx, tr, dr, sh)) return false;
        CHECK_OK(tm.read(p.d.ctx, a));
        CHECK_OK(tr.read(p.d.ctx, b));
        const DiffStats ds = diffStats(a, b, kClear8, nullptr);
        char line[200];
        std::snprintf(line, sizeof line, "%s,%u,%u,%s,%llu,%llu,%u,%u,%u\n", p.ex->name.c_str(), dr.n, dr.view,
                      kindOf(dr) == kForest ? "forest" : "jungle", static_cast<unsigned long long>(ds.drawn),
                      static_cast<unsigned long long>(ds.differing), ds.median, ds.p95, ds.max);
        csv += line;
        res.worst = std::max(res.worst, ds.max);
        res.drawn += ds.drawn;
        res.drawsWithPixels += ds.drawn ? 1u : 0u;
        ++res.draws;
    }
    tm.clear(p.d.ctx, kClear);
    tr.clear(p.d.ctx, kClear);
    for (const DrawRef& dr : p.ex->draws) {
        if (!p.moduleDraw(tm, dr, sh, nullptr)) return false;
        if (!p.rep.draw(p.d.ctx, tr, dr, sh)) return false;
    }
    CHECK_OK(tm.read(p.d.ctx, res.stageModule));
    CHECK_OK(tr.read(p.d.ctx, b));
    res.stage = diffStats(res.stageModule, b, kClear8, &res.stageDiff);
    char line[200];
    std::snprintf(line, sizeof line, "%s,stage,all,all,%llu,%llu,%u,%u,%u\n", p.ex->name.c_str(),
                  static_cast<unsigned long long>(res.stage.drawn), static_cast<unsigned long long>(res.stage.differing),
                  res.stage.median, res.stage.p95, res.stage.max);
    csv += line;
    std::printf("%s: %u draws (%u with pixels on screen), %llu drawn pixels, largest channel difference %u; stage: %llu "
                "drawn, %llu differ, median %u p95 %u max %u\n",
                p.ex->name.c_str(), res.draws, res.drawsWithPixels, static_cast<unsigned long long>(res.drawn), res.worst,
                static_cast<unsigned long long>(res.stage.drawn), static_cast<unsigned long long>(res.stage.differing),
                res.stage.median, res.stage.p95, res.stage.max);
    return true;
}

const char* const kCsvHead = "export,draw,view,kind,drawn_px,differing_px,median,p95,max\n";

} // namespace

C5R_TEST(per_draw_and_stage_equal_the_capture_replay) {
    PerDraw p;
    if (!p.setUp()) return;
    const uint32_t W = 2554, H = 1411;
    Target tm, tr;
    CHECK_OK(tm.create(p.d.dev, W, H));
    CHECK_OK(tr.create(p.d.dev, W, H));
    std::string csv = kCsvHead;
    Shared sh;
    if (!sh.make(p.d.dev, p.d.ctx, p.ex->name, p.ex->draws.front())) return;   // t2-t8 are the same in every draw of L01
    ExportResult res;
    if (!runExport(p, tm, tr, sh, csv, res)) return;
    const std::vector<uint8_t>& a = res.stageModule;
    char line[200];
    // t3 with one mip against t3 with its generated mips (does the tree PS reach the mips? trees.md 11.1)
    {
        Shared one;
        if (!one.make(p.d.dev, p.d.ctx, p.ex->name, p.ex->draws.front(), true)) return;
        Target t1m;
        CHECK_OK(t1m.create(p.d.dev, W, H));
        t1m.clear(p.d.ctx, kClear);
        for (const DrawRef& dr : p.ex->draws)
            if (!p.moduleDraw(t1m, dr, one, nullptr)) return;
        std::vector<uint8_t> c;
        CHECK_OK(t1m.read(p.d.ctx, c));
        const DiffStats m = diffStats(a, c, kClear8, nullptr);
        std::snprintf(line, sizeof line, "%s,stage_t3_one_mip_vs_mips,all,all,%llu,%llu,%u,%u,%u\n", p.ex->name.c_str(),
                      static_cast<unsigned long long>(m.drawn), static_cast<unsigned long long>(m.differing), m.median,
                      m.p95, m.max);
        csv += line;
        std::printf("stage with a one-mip t3 against the generated mips: %llu of %llu drawn pixels differ, max %u\n",
                    static_cast<unsigned long long>(m.differing), static_cast<unsigned long long>(m.drawn), m.max);
    }
    const std::string out = test::workDir() + "/forest_perdraw_L01.csv";
    CHECK(writeFile(out, std::vector<uint8_t>(csv.begin(), csv.end())));
    writeBmp(test::workDir() + "/forest_stage_module_L01.bmp", a, W, H);
    writeBmp(test::workDir() + "/forest_stage_diff_L01.bmp", res.stageDiff, W, H);
    std::printf("csv %s\n", out.c_str());
    CHECK_EQ(res.draws, 46u);
    CHECK(res.drawn > 100000u);
    CHECK_EQ(res.worst, 0u);
    CHECK_EQ(res.stage.max, 0u);
    CHECK(res.stage.drawn > 50000u);
    // the stage's bound state equals stock's (state.json of draw 394 [L])
    ID3D11DeviceContext* c = p.d.ctx;
    tm.clear(c, kClear);
    if (!p.moduleDraw(tm, p.ex->draws.front(), sh, nullptr)) return;
    {
        ID3D11BlendState* bs = nullptr;
        float f[4];
        UINT mask = 0;
        c->OMGetBlendState(&bs, f, &mask);
        CHECK(bs != nullptr);
        if (bs) {
            D3D11_BLEND_DESC dsc;
            bs->GetDesc(&dsc);
            const D3D11_RENDER_TARGET_BLEND_DESC& t = dsc.RenderTarget[0];
            CHECK(t.BlendEnable && t.SrcBlend == D3D11_BLEND_SRC_ALPHA && t.DestBlend == D3D11_BLEND_INV_SRC_ALPHA &&
                  t.BlendOp == D3D11_BLEND_OP_ADD && t.SrcBlendAlpha == D3D11_BLEND_ONE && t.DestBlendAlpha == D3D11_BLEND_ONE &&
                  t.BlendOpAlpha == D3D11_BLEND_OP_ADD && t.RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL);
            CHECK(!dsc.AlphaToCoverageEnable && !dsc.IndependentBlendEnable);
            bs->Release();
        }
        ID3D11DepthStencilState* ds = nullptr;
        UINT ref = 7;
        c->OMGetDepthStencilState(&ds, &ref);
        CHECK(ds != nullptr && ref == 0);
        if (ds) {
            D3D11_DEPTH_STENCIL_DESC dsc;
            ds->GetDesc(&dsc);
            CHECK(dsc.DepthEnable && dsc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL &&
                  dsc.DepthFunc == D3D11_COMPARISON_LESS_EQUAL && !dsc.StencilEnable);
            ds->Release();
        }
        ID3D11RasterizerState* rs = nullptr;
        c->RSGetState(&rs);
        CHECK(rs != nullptr);
        if (rs) {
            D3D11_RASTERIZER_DESC dsc;
            rs->GetDesc(&dsc);
            CHECK(dsc.FillMode == D3D11_FILL_SOLID && dsc.CullMode == D3D11_CULL_BACK && dsc.FrontCounterClockwise &&
                  dsc.DepthClipEnable && dsc.DepthBias == 0 && !dsc.ScissorEnable && !dsc.MultisampleEnable);
            rs->Release();
        }
        ID3D11SamplerState* ss[9] = {};
        c->PSGetSamplers(0, 9, ss);
        for (uint32_t i = 0; i < 9; ++i) {
            CHECK(ss[i] != nullptr);
            if (!ss[i]) continue;
            D3D11_SAMPLER_DESC dsc;
            ss[i]->GetDesc(&dsc);
            const D3D11_TEXTURE_ADDRESS_MODE am = (i == 4 || i == 5 || i == 8) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
            CHECK(dsc.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && dsc.AddressU == am && dsc.AddressV == am && dsc.AddressW == am &&
                  dsc.MipLODBias == 0.0f && dsc.MinLOD == 0.0f && dsc.MaxLOD >= 3.4e38f && dsc.ComparisonFunc == D3D11_COMPARISON_NEVER);
            ss[i]->Release();
        }
        ID3D11ShaderResourceView* srv[9] = {};
        c->PSGetShaderResources(0, 9, srv);
        CHECK(srv[0] == p.r.texture(kAsia, kindOf(p.ex->draws.front()), 0) && srv[1] == p.r.texture(kAsia, kindOf(p.ex->draws.front()), 1));
        for (uint32_t i = 2; i < 9; ++i) CHECK(srv[i] == sh.v[i]);
        for (auto& x : srv) rel(x);
        D3D11_PRIMITIVE_TOPOLOGY topo;
        c->IAGetPrimitiveTopology(&topo);
        CHECK_EQ(topo, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cb[8] = {};
        c->VSGetConstantBuffers(0, 8, cb);
        CHECK(cb[0] == nullptr && cb[1] != nullptr && cb[7] != nullptr);
        for (auto& x : cb) rel(x);
        D3D11_VIEWPORT vp;
        UINT nvp = 1;
        c->RSGetViewports(&nvp, &vp);
        CHECK(nvp == 1 && vp.Width == 2554.0f && vp.Height == 1411.0f && vp.MaxDepth == 1.0f);
    }
}

// The other captures' tree draws: p4_shown_all (88: 44 blocks in two views) and cities/c_ancient (156: 78 in two views),
// each draw with its own view's cb1 block.
C5R_TEST(per_draw_two_view_captures_equal_the_replay) {
    std::string csv = kCsvHead;
    const uint32_t W = 2554, H = 1411;
    for (const char* name : {"p4_shown_all", "cities/c_ancient"}) {
        PerDraw p;
        if (!p.setUp(name)) return;
        Target tm, tr;
        CHECK_OK(tm.create(p.d.dev, W, H));
        CHECK_OK(tr.create(p.d.dev, W, H));
        Shared sh;
        if (!sh.make(p.d.dev, p.d.ctx, p.ex->name, p.ex->draws.front())) return;
        ExportResult res;
        if (!runExport(p, tm, tr, sh, csv, res)) return;
        CHECK_EQ(res.draws, static_cast<uint32_t>(p.ex->draws.size()));
        CHECK_EQ(p.ex->views(), 2u);
        CHECK(res.drawn > 50000u);
        CHECK_EQ(res.worst, 0u);
        CHECK_EQ(res.stage.max, 0u);
        CHECK(res.stage.drawn > 50000u);
        // the stage with a one-mip t3 (mip 0 only) against the generated mips: does the tree PS reach t3's mips? (these
        // captures' t3 holds borders and territory, L01's is empty; trees.md 11.1)
        Shared one;
        if (!one.make(p.d.dev, p.d.ctx, p.ex->name, p.ex->draws.front(), true)) return;
        tm.clear(p.d.ctx, kClear);
        for (const DrawRef& dr : p.ex->draws)
            if (!p.moduleDraw(tm, dr, one, nullptr)) return;
        std::vector<uint8_t> c;
        CHECK_OK(tm.read(p.d.ctx, c));
        const DiffStats m = diffStats(res.stageModule, c, kClear8, nullptr);
        char line[200];
        std::snprintf(line, sizeof line, "%s,stage_t3_one_mip_vs_mips,all,all,%llu,%llu,%u,%u,%u\n", p.ex->name.c_str(),
                      static_cast<unsigned long long>(m.drawn), static_cast<unsigned long long>(m.differing), m.median,
                      m.p95, m.max);
        csv += line;
        std::printf("%s: stage with a one-mip t3 against the generated mips: %llu of %llu drawn pixels differ, max %u\n",
                    p.ex->name.c_str(), static_cast<unsigned long long>(m.differing), static_cast<unsigned long long>(m.drawn),
                    m.max);
        // and the stage without any overlay (t3 all zero), to show that t3 reaches the trees in these captures
        {
            Shared zero;
            if (!zero.make(p.d.dev, p.d.ctx, p.ex->name, p.ex->draws.front(), true)) return;
            ID3D11Resource* res3 = nullptr;
            zero.v[3]->GetResource(&res3);
            std::vector<uint8_t> zeros(static_cast<size_t>(W) * H * 4, 0);
            p.d.ctx->UpdateSubresource(res3, 0, nullptr, zeros.data(), W * 4, 0);
            res3->Release();
            tm.clear(p.d.ctx, kClear);
            for (const DrawRef& dr : p.ex->draws)
                if (!p.moduleDraw(tm, dr, zero, nullptr)) return;
            CHECK_OK(tm.read(p.d.ctx, c));
            const DiffStats z = diffStats(res.stageModule, c, kClear8, nullptr);
            std::snprintf(line, sizeof line, "%s,stage_t3_zero_vs_captured,all,all,%llu,%llu,%u,%u,%u\n", p.ex->name.c_str(),
                          static_cast<unsigned long long>(z.drawn), static_cast<unsigned long long>(z.differing), z.median,
                          z.p95, z.max);
            csv += line;
            std::printf("%s: stage with t3 = 0 against the captured t3: %llu of %llu drawn pixels differ, max %u\n",
                        p.ex->name.c_str(), static_cast<unsigned long long>(z.differing),
                        static_cast<unsigned long long>(z.drawn), z.max);
        }
    }
    const std::string out = test::workDir() + "/forest_perdraw_two_views.csv";
    CHECK(writeFile(out, std::vector<uint8_t>(csv.begin(), csv.end())));
    std::printf("csv %s\n", out.c_str());
}

// Two views draw every block in each (cities/c_ancient: 78 blocks x 2 views); the module's blocks are its frames().
C5R_TEST(draw_views_and_frames) {
    Inputs& g = inputs();
    if (!g.ok) return;
    const ExportRef* ca = findExport("cities/c_ancient");
    if (!ca) return;
    std::vector<std::vector<uint8_t>> cap;
    if (!capturedBlocks(*ca, cap)) return;
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    CHECK_OK(r.init(d.dev, g.sec, g.fs));
    Target t;
    CHECK_OK(t.create(d.dev, 640, 360));
    Shared sh;
    if (!sh.make(d.dev, d.ctx, ca->name, ca->draws.front())) return;
    FrameInputs in = capturedInputs(cap);
    in.width = 640;
    in.height = 360;
    sh.into(in, kAsia);
    std::vector<Block> blocks;
    for (const DrawRef& dr : ca->draws) {
        if (dr.view != 0) continue;
        std::vector<uint8_t> cb7;
        if (!readRef(ca->name, dr.cb7, cb7, kBlockBytes)) return;
        Block b;
        b.entry = kAsia;
        b.kind = kindOf(dr);
        std::memcpy(b.trees, cb7.data(), kBlockBytes);
        blocks.push_back(b);
    }
    CHECK_EQ(blocks.size(), static_cast<size_t>(78));
    FrameStats st;
    t.clear(d.ctx, kClear);
    CHECK_OK(r.draw(d.ctx, t.rtv, t.dsv, in, blocks.data(), blocks.size(), &st));
    CHECK_EQ(st.views, 2u);
    CHECK_EQ(st.draws, 156u);
    CHECK_EQ(st.indices, 156ull * 2160ull);
    CHECK_EQ(r.frames().size(), static_cast<size_t>(2));
    for (uint32_t v = 0; v < 2 && v < r.frames().size(); ++v) {
        // the module's blocks are computeForestFrame's; against the capture only row 9 (the 640 x 360 target) differs
        uint8_t want[kFrameBytes];
        CHECK_OK(computeForestFrame(in, v, want));
        CHECK(std::memcmp(r.frames()[v].data(), want, kFrameBytes) == 0);
        uint8_t masked[kFrameBytes];
        std::memcpy(masked, r.frames()[v].data(), kFrameBytes);
        std::memcpy(masked + 0x90, cap[v].data() + 0x90, 8);
        uint32_t first = 0, ulp = 0;
        CHECK_EQ(maskedDiff(masked, cap[v].data(), 1, &first, &ulp), 0u);
        CHECK(f32(r.frames()[v], 0x90) == 640.0f && f32(r.frames()[v], 0x94) == 360.0f);
    }
    std::vector<uint8_t> px;
    CHECK_OK(t.read(d.ctx, px));
    std::printf("two views, 156 draws: %u of %u pixels drawn (640 x 360 viewport)\n", covered(px, kClear8), 640u * 360u);
    // no blocks: nothing drawn, still a valid call
    t.clear(d.ctx, kClear);
    CHECK_OK(r.draw(d.ctx, t.rtv, t.dsv, in, nullptr, 0, &st));
    CHECK_EQ(st.draws, 0u);
    CHECK_OK(t.read(d.ctx, px));
    CHECK_EQ(covered(px, kClear8), 0u);
}

// ---- (c) refusals ----

C5R_TEST(calls_before_init_fail) {
    Renderer r;
    CHECK(!r.ready());
    shaders::ShaderSection sec;
    gamefs::GameFiles fs;
    CHECK_ERR(r.init(nullptr, sec, fs), Err::BadLayout);
    CHECK(!r.ready());
    CHECK_ERR(r.replaceCards(0, 0, std::vector<uint8_t>(36), std::vector<uint8_t>(6), 3), Err::NotFound);
    FrameInputs in;
    CHECK_ERR(r.draw(nullptr, nullptr, nullptr, in, nullptr, 0, nullptr), Err::NotFound);
    CHECK(r.texture(0, 0, 0) == nullptr);
    CHECK(r.cards(0, 0).empty() && r.choice(0, 0).empty() && r.buffers(0, 0).vb.empty());
    CHECK(r.frames().empty());
}

// init: a missing blob, a blob of another stage, a declaration, a slot or the cb sizes unlike stock's.
C5R_TEST(init_rejects_programs_unlike_stocks) {
    Inputs& g = inputs();
    if (!g.ok) return;
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    InitOptions o;
    o.vsVa = 0x00401234u;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::NotFound);
    o = InitOptions();
    o.psVa = 0x00401234u;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::NotFound);
    o = InitOptions();
    o.vsVa = kForestPsVa;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::BadLayout);
    o = InitOptions();
    o.psVa = kForestVsVa;
    CHECK_ERR(r.init(d.dev, g.sec, g.fs, o), Err::BadLayout);
    CHECK(!r.ready());

    const std::vector<uint8_t> vsReal = blobOf(g.sec, kForestVsVa), psReal = blobOf(g.sec, kForestPsVa);
    CHECK(!vsReal.empty() && !psReal.empty());
    auto vsOf = [](const std::string& src) { return test::stripLikeExe(test::compileHlsl(src.c_str(), "main", "vs_5_0")); };
    const std::vector<uint8_t> vsGood = vsOf(kVsGood);

    const std::vector<uint8_t> vsNoBi = vsOf(
        "cbuffer F : register(b1) { float4 r[9]; }; cbuffer B : register(b7) { float4 t[90]; };\n"
        "float4 main(float3 p : POSITION, float3 n : NORMAL, float2 uv : TEXCOORD0, float4 bi : TEXCOORD1) : SV_Position {\n"
        "  return float4(p * t[(uint)bi.x].w, 1) * r[0] + float4(n, uv.x) * r[8]; }\n");
    std::string s89 = kVsGood;
    s89.replace(s89.find("t[90]"), 5, "t[89]");
    const std::vector<uint8_t> vsCb7of89 = vsOf(s89);
    std::string sCb2 = kVsGood;
    sCb2.replace(sCb2.find("cbuffer B"), 0, "cbuffer X : register(b2) { float4 x; }; ");
    sCb2.replace(sCb2.find("* r[8]"), 6, "* r[8] * x");
    const std::vector<uint8_t> vsCb2 = vsOf(sCb2);
    const std::vector<uint8_t> psGood = ps("", "");
    const std::vector<uint8_t> psT2 = ps("Texture2D t2 : register(t2);\n", "c += t2.Sample(s0, c.xy);\n");
    const std::vector<uint8_t> psT5as2d = [] {
        std::string head = kPsHead;
        head.replace(head.find("Texture3D t5"), 12, "Texture2D t5");
        std::string body = kPsBody;
        body.replace(body.find("t5.Sample(s0, c.xyz)"), 20, "t5.Sample(s0, c.xy)");
        return ps("", "", "14", head.c_str(), body.c_str());
    }();
    const std::vector<uint8_t> psNoT7 = [] {
        std::string body = kPsBody;
        body.replace(body.find(" + t7.Sample(s0, c.xy)"), 22, "");
        return ps("", "", "14", kPsHead, body.c_str());
    }();
    const std::vector<uint8_t> psRows12 = ps("", "", "11");
    const std::vector<uint8_t> psS9 = ps("SamplerState s9 : register(s9);\n", "c += t1.Sample(s9, c.yx);\n");
    for (const auto* b : {&vsGood, &vsNoBi, &vsCb7of89, &vsCb2, &psGood, &psT2, &psT5as2d, &psNoT7, &psRows12, &psS9})
        CHECK(!b->empty());
    std::vector<uint32_t> va;
    shaders::ShaderSection syn;
    CHECK_OK(syn.parse(sectionWith({vsReal, psReal, vsGood, vsNoBi, vsCb7of89, vsCb2, psGood, psT2, psT5as2d, psNoT7, psRows12, psS9}, va)));
    if (va.size() != 12) return;
    auto tryInit = [&](uint32_t vs, uint32_t psVa) {
        InitOptions io;
        io.vsVa = vs;
        io.psVa = psVa;
        return r.init(d.dev, syn, g.fs, io);
    };
    CHECK_OK(tryInit(va[0], va[1]));                  // the EXE's own programs from the synthetic section
    CHECK(r.ready());
    CHECK_OK(tryInit(va[2], va[6]));                  // stand-ins declared exactly as stock's pass
    CHECK_ERR(tryInit(va[3], va[1]), Err::BadLayout); // VS input without BLENDINDICES
    CHECK(!r.ready());                                // the good init is gone, nothing half-made
    CHECK_ERR(tryInit(va[4], va[1]), Err::BadLayout); // cb7 of 89 rows
    CHECK_ERR(tryInit(va[5], va[1]), Err::BadLayout); // VS declares cb2
    CHECK_ERR(tryInit(va[0], va[7]), Err::BadLayout); // t2 declared
    CHECK_ERR(tryInit(va[0], va[8]), Err::BadLayout); // t5 a texture2d
    CHECK_ERR(tryInit(va[0], va[9]), Err::BadLayout); // t7 missing
    CHECK_ERR(tryInit(va[0], va[10]), Err::BadLayout); // cb1 of 12 rows
    CHECK_ERR(tryInit(va[0], va[11]), Err::BadLayout); // sampler s9
    CHECK(!r.ready());
}

// init: a model or texture file missing from an install fails NotFound naming it; a damaged one fails with the parser's
// reason naming it.
C5R_TEST(init_names_a_missing_or_damaged_file) {
    Inputs& g = inputs();
    if (!g.ok) return;
    Device d;
    CHECK_OK(d.create());
    std::vector<std::string> files;
    for (uint32_t e = 0; e < 4; ++e)
        for (uint32_t k = 0; k < kKinds; ++k) {
            files.push_back(modelFile(e, k));
            files.push_back(diffuseFile(e, k));
            files.push_back(overlayFile(e, k));
        }
    std::vector<std::vector<uint8_t>> bytes(files.size());
    for (size_t i = 0; i < files.size(); ++i) CHECK_OK(g.fs.read(files[i], bytes[i]));
    const std::string root = test::workDir() + "/forest_fake_install";
    CreateDirectoryW(wide(root).c_str(), nullptr);
    auto install = [&](const std::string& name, int missing, int damaged, size_t cutTo) {
        const std::string inst = root + "/" + name;
        CreateDirectoryW(wide(inst).c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Assets").c_str(), nullptr);
        CreateDirectoryW(wide(inst + "/Resource").c_str(), nullptr);
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string p = inst + "/Assets/" + files[i];
            if (static_cast<int>(i) == missing) {
                DeleteFileW(wide(p).c_str());
                continue;
            }
            std::vector<uint8_t> b = bytes[i];
            if (static_cast<int>(i) == damaged) b.resize(cutTo);
            CHECK(writeFile(p, b));
        }
        return inst;
    };
    // missing: a model, a diffuse texture, an overlay texture (Asia jungle: indices 3 x (2 x 2 + 1) + 0..2)
    const int jungleAsia = 3 * (2 * 2 + 1);
    for (int which = 0; which < 3; ++which) {
        const std::string inst = install("m" + std::to_string(which), jungleAsia + which, -1, 0);
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        const Status st = r.init(d.dev, g.sec, fs);
        CHECK_ERR(st, Err::NotFound);
        CHECK(st.msg.find(files[static_cast<size_t>(jungleAsia + which)]) != std::string::npos);
        CHECK(!r.ready());
    }
    // every file present: the same install inits
    {
        const std::string inst = install("all", -1, -1, 0);
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        CHECK_OK(r.init(d.dev, g.sec, fs));
        CHECK(r.ready());
    }
    // damaged: a DDS cut inside its header, a model cut in half
    {
        const std::string inst = install("bad_dds", -1, jungleAsia + 1, 100);
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        const Status st = r.init(d.dev, g.sec, fs);
        CHECK_ERR(st, Err::Truncated);
        CHECK(st.msg.find(files[static_cast<size_t>(jungleAsia + 1)]) != std::string::npos);
        CHECK(!r.ready());
    }
    {
        const std::string inst = install("bad_gr2", -1, jungleAsia, bytes[static_cast<size_t>(jungleAsia)].size() / 2);
        gamefs::GameFiles fs;
        CHECK_OK(fs.open(wide(inst)));
        Renderer r;
        const Status st = r.init(d.dev, g.sec, fs);
        CHECK(!st.ok());
        CHECK(st.msg.find(files[static_cast<size_t>(jungleAsia)]) != std::string::npos);
        CHECK(!r.ready());
    }
}

// replaceCards: every check; a failed call keeps the previous buffers.
C5R_TEST(replace_cards_refusals_keep_the_buffers) {
    Inputs& g = inputs();
    if (!g.ok) return;
    const ExportRef* ex = findExport(kL01);
    if (!ex) return;
    Device d;
    CHECK_OK(d.create());
    Renderer r;
    CHECK_OK(r.init(d.dev, g.sec, g.fs));
    const DrawRef& dr = ex->draws.front();
    std::vector<uint8_t> vb, ib;
    if (!readRef(ex->name, dr.vb, vb, dr.vbBytes) || !readRef(ex->name, dr.ib, ib, dr.ibBytes)) return;
    const uint32_t k = kindOf(dr);
    const CardBuffers before = r.buffers(kAsia, k);
    auto bad = [&](uint32_t e, uint32_t kind, const std::vector<uint8_t>& v, const std::vector<uint8_t>& i, uint32_t n) {
        CHECK_ERR(r.replaceCards(e, kind, v, i, n), Err::BadLayout);
        CHECK(r.buffers(kAsia, k).vb == before.vb && r.buffers(kAsia, k).ib == before.ib &&
              r.buffers(kAsia, k).indexCount == before.indexCount);
    };
    bad(5, k, vb, ib, 2160);
    bad(kAsia, 2, vb, ib, 2160);
    bad(kAsia, k, std::vector<uint8_t>(), ib, 2160);
    std::vector<uint8_t> v2 = vb;
    v2.resize(v2.size() - 4);
    bad(kAsia, k, v2, ib, 2160);                     // not whole vertices
    std::vector<uint8_t> i2 = ib;
    i2.resize(i2.size() - 1);
    bad(kAsia, k, vb, i2, 2160);                     // not whole indices
    bad(kAsia, k, vb, ib, 0);
    bad(kAsia, k, vb, ib, 2159);                     // not whole triangles
    bad(kAsia, k, vb, ib, 2163);                     // past the IB
    v2 = vb;
    v2.resize(36 * 100);                             // indices past the VB
    bad(kAsia, k, v2, ib, 2160);
    v2 = vb;
    v2[32] = 90;                                     // vertex 0 with slot 90
    bad(kAsia, k, v2, ib, 2160);
    CHECK_OK(r.replaceCards(kAsia, k, vb, ib, dr.indexCount));
    CHECK(r.buffers(kAsia, k).vb == vb);
    CHECK(r.choice(kAsia, k).empty());
}

// draw: every check; a failed draw draws nothing.
C5R_TEST(draw_refusals_draw_nothing) {
    PerDraw p;
    if (!p.setUp()) return;
    Target t;
    CHECK_OK(t.create(p.d.dev, 320, 180));
    Shared sh;
    // draw 428 of L01: the draw with the most pixels on screen (18,862 at 2554 x 1411; forest_perdraw_L01.csv)
    const DrawRef* pick = nullptr;
    for (const DrawRef& x : p.ex->draws)
        if (x.n == 428) pick = &x;
    CHECK(pick != nullptr);
    if (!pick) return;
    const DrawRef& dr = *pick;
    if (!sh.make(p.d.dev, p.d.ctx, p.ex->name, dr)) return;
    std::vector<uint8_t> cb1, cb7;
    if (!readRef(p.ex->name, dr.cb1, cb1, kFrameBytes) || !readRef(p.ex->name, dr.cb7, cb7, kBlockBytes)) return;
    FrameInputs good = capturedInputs({cb1});
    good.width = 320;
    good.height = 180;
    sh.into(good, kAsia);
    Block gb;
    gb.entry = kAsia;
    gb.kind = kindOf(dr);
    std::memcpy(gb.trees, cb7.data(), kBlockBytes);
    // the good call draws
    t.clear(p.d.ctx, kClear);
    CHECK_OK(p.r.draw(p.d.ctx, t.rtv, t.dsv, good, &gb, 1, nullptr));
    std::vector<uint8_t> px;
    CHECK_OK(t.read(p.d.ctx, px));
    CHECK(covered(px, kClear8) > 100u);
    auto failsClean = [&](const FrameInputs& in, const Block* b, size_t n, Err want, const char* name) {
        t.clear(p.d.ctx, kClear);
        const Status st = p.r.draw(p.d.ctx, t.rtv, t.dsv, in, b, n, nullptr);
        CHECK_ERR(st, want);
        if (name) CHECK(st.msg.find(name) != std::string::npos);
        CHECK_OK(t.read(p.d.ctx, px));
        CHECK_EQ(covered(px, kClear8), 0u);
    };
    FrameInputs in = good;
    in.screenOverlay = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t3");
    in = good;
    in.fogRamp = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t4");
    in = good;
    in.fowMask = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t6");
    in = good;
    in.cloudBase = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t7");
    in = good;
    in.colorKeyLut[lutIndex(kAsia)] = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t5");
    in = good;
    in.fowLut[lutIndex(kAsia)] = nullptr;
    failsClean(in, &gb, 1, Err::NotFound, "t8");
    // a LUT of another entry missing: the Asia block still needs only its own
    in = good;
    in.colorKeyLut[lutIndex(1)] = nullptr;
    in.fowLut[lutIndex(1)] = nullptr;
    t.clear(p.d.ctx, kClear);
    CHECK_OK(p.r.draw(p.d.ctx, t.rtv, t.dsv, in, &gb, 1, nullptr));
    // a block of entry 1 (America: LUT 3) without its LUTs fails naming LUT 3; the good Asia block before it is not drawn
    Block two[2] = {gb, gb};
    two[1].entry = 1;
    failsClean(in, two, 2, Err::NotFound, "LUT 3");
    Block b = gb;
    b.entry = 5;
    failsClean(good, &b, 1, Err::BadLayout, "entry");
    b = gb;
    b.kind = 2;
    failsClean(good, &b, 1, Err::BadLayout, "kind");
    b = gb;
    b.trees[200] = NAN;
    failsClean(good, &b, 1, Err::BadLayout, "not finite");
    failsClean(good, nullptr, 1, Err::BadLayout, "blocks null");
    in = good;
    in.numViews = 0;
    failsClean(in, &gb, 1, Err::BadLayout, "numViews");
    in = good;
    in.views[0].view[0] = NAN;
    failsClean(in, &gb, 1, Err::BadLayout, nullptr);
    in = good;
    in.width = 0;
    failsClean(in, &gb, 1, Err::BadLayout, nullptr);
    // two views where the second is bad: nothing drawn in the first either
    in = good;
    in.numViews = 2;
    in.views[1] = in.views[0];
    in.views[1].overlayProj[0] = NAN;
    failsClean(in, &gb, 1, Err::BadLayout, nullptr);
    CHECK_ERR(p.r.draw(nullptr, t.rtv, t.dsv, good, &gb, 1, nullptr), Err::BadLayout);
    CHECK_ERR(p.r.draw(p.d.ctx, nullptr, t.dsv, good, &gb, 1, nullptr), Err::BadLayout);
}

// ---- the Screen Overlay pass of c5r::terrain (ticket 4551, trees.md 11.2) on synthetic fields ----

namespace {

// A field of mapW x mapH plots of one height byte (the drawn surface is flat at z = byte * 0.2), centred on the origin
// (as render/tests/terrain's makeGrid).
heightfield::HeightGrid flatField(uint32_t mapW, uint32_t mapH, uint8_t heightByte) {
    heightfield::HeightGrid g;
    g.mapW = mapW;
    g.mapH = mapH;
    g.samplesX = mapW * 64;
    g.samplesY = mapH * 64;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(mapW * 32);
    g.oy = static_cast<float>(mapH) * 32 * 0.866f;
    g.waterZ = 10.117f;
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, static_cast<uint16_t>(heightByte << 8));
    return g;
}

// A camera at (0, 0, height) looking straight down, RH perspective, fov 30 degrees.
terrain::Camera downCamera(float height, uint32_t w, uint32_t h) {
    terrain::Camera c;
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -height, 1};
    std::memcpy(c.view, v, sizeof v);
    const float n = 1.0f, f = 2000.0f;
    const float ys = 1.0f / std::tan(15.0f * 3.14159265f / 180.0f), xs = ys * static_cast<float>(h) / static_cast<float>(w);
    const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, f / (n - f), -1, 0, 0, n * f / (n - f), 0};
    std::memcpy(c.proj, p, sizeof p);
    c.width = w;
    c.height = h;
    return c;
}

ID3D11ShaderResourceView* solid(ID3D11Device* dev, uint32_t rgba) {
    std::vector<uint32_t> px(16, rgba);
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), 16, 0};
    ID3D11Texture2D* t = nullptr;
    ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, &s, &t))) return nullptr;
    dev->CreateShaderResourceView(t, nullptr, &v);
    t->Release();
    return v;
}

// An RGBA8 target of a format with readback (the screen overlay's).
struct PlainTarget {
    ID3D11Texture2D* tex = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    uint32_t w = 0, h = 0;
    PlainTarget(ID3D11Device* dev, uint32_t width, uint32_t height, DXGI_FORMAT f) : w(width), h(height) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &tex))) dev->CreateRenderTargetView(tex, nullptr, &rtv);
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dev->CreateTexture2D(&d, nullptr, &staging);
    }
    ~PlainTarget() {
        rel(rtv);
        rel(staging);
        rel(tex);
    }
    std::vector<uint32_t> read(ID3D11DeviceContext* ctx) {
        std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0);
        ctx->CopyResource(staging, tex);
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return px;
        for (uint32_t y = 0; y < h; ++y)
            std::memcpy(&px[static_cast<size_t>(y) * w], static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, w * 4);
        ctx->Unmap(staging, 0);
        return px;
    }
};

struct OverlayFixture {
    Device d;
    terrain::Renderer r;
    bool ok = false;
    OverlayFixture() {
        const std::string exe = test::exePath();
        if (exe.empty()) test::skip("C5R_EXE not set: the TerrainShader programs come from the EXE");
        CHECK_OK(d.create());
        if (!d.dev) return;
        const Status st = r.init(d.dev, wide(exe), L"");
        CHECK_OK(st);
        ok = st.ok();
    }
};

} // namespace

// The pass draws the overlay's colour wherever the scene draws land, 0 elsewhere; the same tiles as the scene draw,
// pass 2, the patch list of level L + 1 (LodStep twice the scene's) at the scene's LodMip; the LOD state and the
// depth are untouched; no depth target, no blend, no culling bound.
C5R_TEST(screen_overlay_pass_projects_the_overlay_onto_the_land) {
    OverlayFixture fx;
    if (!fx.ok) return;
    const uint32_t W = 320, H = 200;
    CHECK_OK(fx.r.setHeightField(flatField(2, 2, 100), 1));
    const terrain::Camera cam = downCamera(400.0f, W, H);
    ID3D11ShaderResourceView* ovl = solid(fx.d.dev, 0xC8786432u);   // r 0x32, g 0x64, b 0x78, a 0xC8 (premultiplied-looking)
    CHECK(ovl != nullptr);
    terrain::Options o;
    o.water = false;
    o.color = terrain::ColorSource::Constant;
    o.constantRgba = 0xFF00FF00u;
    o.clearRgba = 0xFF000000u;
    o.overlay.srv = ovl;
    for (int level : {terrain::kLodExe, 1, terrain::kMaxLodLevel}) {
        o.lodLevel = level;
        PlainTarget scene(fx.d.dev, W, H, DXGI_FORMAT_R8G8B8A8_UNORM), screen(fx.d.dev, W, H, DXGI_FORMAT_R8G8B8A8_UNORM);
        terrain::FrameStats ss, so;
        CHECK_OK(fx.r.draw(fx.d.ctx, scene.rtv, nullptr, cam, o, &ss));
        const terrain::LodState before = fx.r.lodState();
        CHECK_OK(fx.r.drawScreenOverlay(fx.d.ctx, screen.rtv, cam, o, &so));
        const terrain::LodState after = fx.r.lodState();
        CHECK(before.lodMip == after.lodMip && before.value == after.value && before.tessFactor == after.tessFactor);
        CHECK_EQ(so.passUsed, 2);
        CHECK_EQ(so.tilesDrawn, ss.tilesDrawn);
        CHECK_EQ(so.tilesCulled, ss.tilesCulled);
        CHECK(so.lodMip == ss.lodMip);
        if (ss.lodLevel < terrain::kMaxLodLevel) CHECK(so.lodStep == 2.0f * ss.lodStep);
        else CHECK(so.lodStep == ss.lodStep);   // the clamp: level 4's own list [I]
        // the pictures: land in the scene (green) <-> the overlay colour in the screen overlay; elsewhere black / 0
        const std::vector<uint32_t> a = scene.read(fx.d.ctx), b = screen.read(fx.d.ctx);
        uint32_t land = 0, both = 0, ovlOnly = 0, wrong = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            const bool isLand = a[i] == 0xFF00FF00u;
            const bool isOvl = b[i] == 0xC8786432u;
            land += isLand;
            both += isLand && isOvl;
            ovlOnly += !isLand && isOvl;
            wrong += !isOvl && b[i] != 0u;
        }
        std::printf("screen overlay, level %d (drawn %d, step %.0f vs %.0f): land %u px, overlay on land %u, overlay off land "
                    "%u, other values %u, tiles %u\n",
                    level, so.lodLevel, static_cast<double>(so.lodStep), static_cast<double>(ss.lodStep), land, both, ovlOnly,
                    wrong, so.tilesDrawn);
        CHECK(land > 1000u);
        CHECK_EQ(wrong, 0u);
        // a flat field: the coarser surface covers the same pixels but for its edge (at most a few rows of pixels)
        CHECK(both + 2u * (W + H) >= land);
        CHECK(ovlOnly <= 2u * (W + H));
        // the bound state: no depth target, depth off, blend off
        ID3D11RenderTargetView* rt = nullptr;
        ID3D11DepthStencilView* ds = nullptr;
        fx.d.ctx->OMGetRenderTargets(1, &rt, &ds);
        CHECK(rt == screen.rtv && ds == nullptr);
        rel(rt);
        rel(ds);
        ID3D11DepthStencilState* dss = nullptr;
        UINT ref = 0;
        fx.d.ctx->OMGetDepthStencilState(&dss, &ref);
        CHECK(dss != nullptr);
        if (dss) {
            D3D11_DEPTH_STENCIL_DESC dd;
            dss->GetDesc(&dd);
            CHECK(!dd.DepthEnable);
            dss->Release();
        }
        ID3D11BlendState* bs = nullptr;
        float bf[4];
        UINT mask = 0;
        fx.d.ctx->OMGetBlendState(&bs, bf, &mask);
        if (bs) {
            D3D11_BLEND_DESC bd;
            bs->GetDesc(&bd);
            CHECK(!bd.RenderTarget[0].BlendEnable);
            bs->Release();
        }
    }
    // without an overlay texture (the transparent default) the pass leaves the target 0
    {
        terrain::Options n = o;
        n.overlay.srv = nullptr;
        n.lodLevel = 1;
        PlainTarget screen(fx.d.dev, W, H, DXGI_FORMAT_R8G8B8A8_UNORM);
        terrain::FrameStats so;
        CHECK_OK(fx.r.drawScreenOverlay(fx.d.ctx, screen.rtv, cam, n, &so));
        CHECK(so.tilesDrawn > 0);
        const std::vector<uint32_t> b = screen.read(fx.d.ctx);
        uint32_t nonzero = 0;
        for (uint32_t v : b) nonzero += v != 0u;
        CHECK_EQ(nonzero, 0u);
    }
    rel(ovl);
}

// Refusals: before init, before a field, a target of another format or size, a bad overlay matrix; nothing changes the
// LOD state.
C5R_TEST(screen_overlay_pass_refusals) {
    {
        terrain::Renderer r;
        Device d;
        CHECK_OK(d.create());
        PlainTarget t(d.dev, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM);
        CHECK_ERR(r.drawScreenOverlay(d.ctx, t.rtv, downCamera(400.0f, 64, 64), terrain::Options(), nullptr), Err::NotFound);
    }
    OverlayFixture fx;
    if (!fx.ok) return;
    const uint32_t W = 160, H = 100;
    const terrain::Camera cam = downCamera(400.0f, W, H);
    PlainTarget good(fx.d.dev, W, H, DXGI_FORMAT_R8G8B8A8_UNORM);
    terrain::Options o;
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, good.rtv, cam, o, nullptr), Err::NotFound);   // no field yet
    CHECK_OK(fx.r.setHeightField(flatField(2, 2, 100), 1));
    CHECK_OK(fx.r.draw(fx.d.ctx, good.rtv, nullptr, cam, o, nullptr));
    const terrain::LodState before = fx.r.lodState();
    PlainTarget bgra(fx.d.dev, W, H, DXGI_FORMAT_B8G8R8A8_UNORM);
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, bgra.rtv, cam, o, nullptr), Err::BadLayout);
    PlainTarget flt(fx.d.dev, W, H, DXGI_FORMAT_R32G32B32A32_FLOAT);
    o.color = terrain::ColorSource::WorldPos;   // the scene's WorldPos target does not make a float overlay target valid
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, flt.rtv, cam, o, nullptr), Err::BadLayout);
    o = terrain::Options();
    PlainTarget narrow(fx.d.dev, W / 2, H, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, narrow.rtv, cam, o, nullptr), Err::BadLayout);
    CHECK_ERR(fx.r.drawScreenOverlay(nullptr, good.rtv, cam, o, nullptr), Err::BadLayout);
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, nullptr, cam, o, nullptr), Err::BadLayout);
    o.overlay.proj[0][13] = NAN;
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, good.rtv, cam, o, nullptr), Err::BadLayout);
    o = terrain::Options();
    o.lodLevel = 7;
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, good.rtv, cam, o, nullptr), Err::BadLayout);
    terrain::Camera bad = cam;
    bad.view[0] = NAN;
    CHECK_ERR(fx.r.drawScreenOverlay(fx.d.ctx, good.rtv, bad, terrain::Options(), nullptr), Err::BadLayout);
    const terrain::LodState after = fx.r.lodState();
    CHECK(before.lodMip == after.lodMip && before.value == after.value && before.hyst == after.hyst);
}

C5R_TEST_MAIN
