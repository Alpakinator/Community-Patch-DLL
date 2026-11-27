// c5r::water coast-wave tests (issue 3606): the wave object and WaveSettings parsers, WaveFrame, the update and the
// atlas cells on synthetic data; the renderer's checks; with C5R_EXE and C5R_GAME_DIR (exit 77 = SKIP without them) the
// EXE's WaveShader drawing a synthetic chunk, and WaveSettings.xml of the install.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/water/waves.h"
#include "testkit.h"

using Microsoft::WRL::ComPtr;
using namespace c5r;
using namespace c5r::water;

namespace {

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void put(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }
void putf(std::vector<uint8_t>& b, size_t off, float v) { std::memcpy(&b[off], &v, 4); }

// A synthetic Wave_System object with settings of the same kind as the shipped file (values chosen here).
std::vector<uint8_t> makeObject() {
    std::vector<uint8_t> b(kWaveObjectBytes, 0);
    put(b, 0x04, 12345);
    put(b, 0x08, 4);
    put(b, 0x0C, 2);
    put(b, 0x10, 7);
    putf(b, 0x1C, 0.1f);
    putf(b, 0x20, 0.05f);
    putf(b, 0x24, 10.0f);
    putf(b, 0x28, 0.5f);
    putf(b, 0x2C, 2.5f);
    putf(b, 0x30, 7.0f);
    putf(b, 0x34, 9.0f);
    putf(b, 0x38, 1.0f);
    putf(b, 0x3C, 3.0f);
    putf(b, 0x40, 0.25f);
    putf(b, 0x44, 0.5f);
    putf(b, 0x48, 0.75f);
    putf(b, 0x50, 128.0f);
    put(b, 0x58, 2);
    put(b, 0x60, 300);
    return b;
}

const char* kXml =
    "<!-- comment with StartDistance=\"9\" inside -->\n<WaveSettings\n  WavesPerEdge=\"1.5\"\n  CrestPagesU=\"4\" CrestPagesV=\"2\"\n"
    "  CrestPagesInUse=\"7\"\n  StartDistance=\"0.1\" EndDistance=\"0.05\"\n  CycleTime=\"10\" FadeInStart=\"0.5\" FadeInEnd=\"2.5\"\n"
    "  FadeOutStart=\"7\" FadeOutEnd=\"9\" RestartDelayMin=\"1\" RestartDelayMax=\"3\"\n  Color_R =\"255\" Color_G =\"0\" Color_B=\"128\"/>\n";

float f32(const uint8_t* b, int i) {
    float v;
    std::memcpy(&v, b + 4 * i, 4);
    return v;
}

struct Dev {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool create() {
        const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return true;
        return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx));
    }
};

uint16_t half(float f) {   // exact for the small integers used here
    if (f == 0.0f) return 0;
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000, e = ((x >> 23) & 0xff) - 127 + 15, m = (x >> 13) & 0x3ff;
    return static_cast<uint16_t>(sign | (e << 10) | m);
}

// One wave quad of 4 vertices around (cx, cy): corners (0/1, 0/1) and uv the same; blend index `wave`.
WaveChunk quadChunk(float cx, float cy, uint8_t wave, uint32_t waveCount) {
    WaveChunk c;
    c.waveCount = waveCount;
    const uint8_t corners[4][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}};
    for (const auto& k : corners) {
        uint8_t v[12] = {0};
        const uint16_t hx = half(cx), hy = half(cy);
        std::memcpy(v, &hx, 2);
        std::memcpy(v + 2, &hy, 2);
        v[4] = k[0];
        v[5] = k[1];
        v[6] = k[0];
        v[7] = k[1];
        v[8] = wave;
        c.vb.insert(c.vb.end(), v, v + 12);
    }
    const uint16_t ix[6] = {0, 1, 2, 2, 1, 3};
    c.ib.assign(reinterpret_cast<const uint8_t*>(ix), reinterpret_cast<const uint8_t*>(ix) + 12);
    return c;
}

} // namespace

// ---------------------------------------------------------------- parsers

C5R_TEST(wave_object_parse) {
    std::vector<uint8_t> b = makeObject();
    WaveObject w;
    CHECK_OK(parseWaveObject(b.data(), b.size(), w));
    CHECK(w.seed == 12345 && w.pagesU == 4 && w.pagesV == 2 && w.pagesInUse == 7);
    CHECK(w.startDistance == 0.1f && w.endDistance == 0.05f && w.cycleTime == 10.0f);
    CHECK(w.fadeInStart == 0.5f && w.fadeInEnd == 2.5f && w.fadeOutStart == 7.0f && w.fadeOutEnd == 9.0f);
    CHECK(w.restartDelayMin == 1.0f && w.restartDelayMax == 3.0f && w.tint[2] == 0.75f && w.hexDiameter == 128.0f);
    CHECK(w.chunkCount == 2 && w.waveCount == 300);
}

C5R_TEST(wave_object_rejects) {
    const std::vector<uint8_t> good = makeObject();
    WaveObject w;
    CHECK(parseWaveObject(nullptr, 0, w).code == Err::Truncated);
    CHECK(parseWaveObject(good.data(), kWaveObjectBytes - 1, w).code == Err::Truncated);
    struct Bad { size_t off; bool isFloat; float f; uint32_t u; };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Bad bad[] = {
        {0x08, false, 0, 0},       // pagesU 0
        {0x0C, false, 0, 0},       // pagesV 0
        {0x10, false, 0, 0},       // no page in use
        {0x10, false, 0, 9},       // more pages than 4 x 2
        {0x24, true, 0.0f, 0},     // cycle time 0
        {0x24, true, nan, 0},      // NaN
        {0x2C, true, 0.5f, 0},     // fade in ends where it starts (division by zero in the frame)
        {0x34, true, 6.0f, 0},     // fade out ends before it starts
        {0x3C, true, 0.5f, 0},     // restart delay max < min
        {0x50, true, 0.0f, 0},     // hex diameter 0
        {0x44, true, std::numeric_limits<float>::infinity(), 0},
    };
    for (const Bad& x : bad) {
        std::vector<uint8_t> b = good;
        if (x.isFloat) putf(b, x.off, x.f); else put(b, x.off, x.u);
        CHECK(parseWaveObject(b.data(), b.size(), w).code == Err::BadLayout);
        CHECK(w.pagesU == 0);   // a failed parse leaves out empty
    }
}

C5R_TEST(wave_settings_xml) {
    WaveObject w;
    CHECK_OK(waveObjectFromSettings(kXml, 128.0f, w));
    CHECK(w.pagesU == 4 && w.pagesV == 2 && w.pagesInUse == 7 && w.seed == 0);
    CHECK(w.startDistance == 0.1f && w.cycleTime == 10.0f && w.restartDelayMax == 3.0f && w.hexDiameter == 128.0f);
    CHECK(w.tint[0] == 1.0f && w.tint[1] == 0.0f && std::fabs(w.tint[2] - static_cast<float>(std::pow(128 / 255.0, 2.2))) < 1e-6f);
    std::string x = kXml;
    CHECK(waveObjectFromSettings(x.substr(0, x.find("CycleTime")), 128.0f, w).code == Err::BadLayout);   // attributes missing
    std::string y = kXml;
    y.replace(y.find("\"4\""), 3, "\"4.5\"");   // a fractional page count
    CHECK(waveObjectFromSettings(y, 128.0f, w).code == Err::BadLayout);
    std::string z = kXml;
    z.replace(z.find("\"10\""), 4, "\"ten\"");
    CHECK(waveObjectFromSettings(z, 128.0f, w).code == Err::BadLayout);
    CHECK(waveObjectFromSettings(kXml, 0.0f, w).code == Err::BadLayout);   // hex diameter 0
    CHECK(waveObjectFromSettings(std::string(kXml) + "<!-- open", 128.0f, w).code == Err::BadLayout);   // unclosed comment
}

// ---------------------------------------------------------------- WaveFrame, atlas, update

C5R_TEST(wave_frame_layout) {
    WaveObject w;
    CHECK_OK(waveObjectFromSettings(kXml, 128.0f, w));
    float vp[16];
    for (int i = 0; i < 16; ++i) vp[i] = static_cast<float>(i + 1);
    uint8_t b[kWaveFrameBytes];
    std::memset(b, 0xcd, sizeof b);
    CHECK_OK(computeWaveFrame(vp, w, 11.0f, b));
    for (int i = 0; i < 16; ++i) CHECK(f32(b, i) == vp[i]);
    CHECK(f32(b, 0x10) == 128.0f * 0.1f && f32(b, 0x11) == 128.0f * 0.05f && f32(b, 0x12) == 0.1f && f32(b, 0x13) == 11.0f);
    CHECK(f32(b, 0x14) == 0.5f && f32(b, 0x15) == 0.5f && f32(b, 0x16) == 7.0f && f32(b, 0x17) == 0.5f);
    CHECK(f32(b, 0x18) == 0.25f && f32(b, 0x19) == 0.5f && f32(b, 0x1a) == 0.0f && f32(b, 0x1b) == 0.0f);
    CHECK(f32(b, 0x1c) == w.tint[0] && f32(b, 0x1e) == w.tint[2] && f32(b, 0x1f) == 1.0f);
    CHECK(computeWaveFrame(vp, w, 11.0f, nullptr).code == Err::BadLayout);
    CHECK(computeWaveFrame(nullptr, w, 11.0f, b).code == Err::BadLayout);
    vp[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK(computeWaveFrame(vp, w, 11.0f, b).code == Err::BadLayout);
    vp[5] = 1;
    CHECK(computeWaveFrame(vp, w, std::numeric_limits<float>::infinity(), b).code == Err::BadLayout);
    WaveObject bad = w;
    bad.pagesV = 0;
    CHECK(computeWaveFrame(vp, bad, 11.0f, b).code == Err::BadLayout);
}

C5R_TEST(wave_atlas_cells) {
    WaveObject w;
    CHECK_OK(waveObjectFromSettings(kXml, 128.0f, w));   // 4 x 2 pages
    float u = -1, v = -1;
    waveAtlasCell(w, 0, &u, &v);
    CHECK(u == 0.0f && v == 0.0f);
    waveAtlasCell(w, 6, &u, &v);
    CHECK(u == 0.5f && v == 0.5f);
    waveAtlasCell(w, 3, &u, &v);
    CHECK(u == 0.75f && v == 0.0f);
}

C5R_TEST(wave_update) {
    std::vector<uint8_t> ob = makeObject();
    WaveObject w;
    CHECK_OK(parseWaveObject(ob.data(), ob.size(), w));
    std::vector<WaveRecord> waves(3);
    waves[0].age = 1.0f;
    waves[1].age = 9.9f;     // passes the 10 s cycle with dt 0.2
    waves[2].age = 9.95f;    // too
    waves[1].u = waves[2].u = 0.99f;
    std::vector<WaveRecord> again = waves;
    CHECK_OK(updateWaves(w, 0.2f, waves));
    CHECK(waves[0].age == 1.2f && waves[0].u == 0.0f);
    for (int i = 1; i < 3; ++i) {
        CHECK(waves[i].age <= -w.restartDelayMin && waves[i].age >= -w.restartDelayMax);   // a restart delay
        CHECK(waves[i].u != 0.99f);                                                       // a new page
        CHECK(std::fmod(waves[i].u * 4.0f, 1.0f) == 0.0f && std::fmod(waves[i].v * 2.0f, 1.0f) == 0.0f);
    }
    CHECK(waves[1].age != waves[2].age);   // the LCG state runs on from one restart to the next
    // the state starts again from the seed at every call: the same input gives the same output
    CHECK_OK(updateWaves(w, 0.2f, again));
    CHECK(std::memcmp(again.data(), waves.data(), sizeof(WaveRecord) * 3) == 0);
    // the first restart of a call uses lcg(seed) for the delay and lcg(lcg(seed)) % pagesInUse for the page
    const uint32_t a = (12345u * 0x343FDu + 0x269EC3u) % 0xFFFFCFEFu;
    const uint32_t b = (a * 0x343FDu + 0x269EC3u) % 0xFFFFCFEFu;
    const double r = static_cast<double>(a) / 4294954991.0;
    CHECK(std::fabs(waves[1].age - static_cast<float>(-1.0 - r * 2.0)) < 1e-6f);
    float u = 0, v = 0;
    waveAtlasCell(w, b % 7, &u, &v);
    CHECK(waves[1].u == u && waves[1].v == v);
    // a wave exactly at the cycle time does not restart (the EXE restarts only past it)
    std::vector<WaveRecord> edge(1);
    edge[0].age = 10.0f;
    CHECK_OK(updateWaves(w, 0.0f, edge));
    CHECK(edge[0].age == 10.0f);
    CHECK(updateWaves(w, -0.1f, waves).code == Err::BadLayout);
    CHECK(updateWaves(w, std::numeric_limits<float>::quiet_NaN(), waves).code == Err::BadLayout);
    WaveObject bad = w;
    bad.pagesInUse = 0;
    CHECK(updateWaves(bad, 0.1f, waves).code == Err::BadLayout);
}

// ---------------------------------------------------------------- the renderer

C5R_TEST(wave_renderer_state_checks) {
    WaveRenderer r;
    Dev d;
    CHECK(d.create());
    std::vector<WaveRecord> waves(1);
    float vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    WaveObject w;
    CHECK_OK(waveObjectFromSettings(kXml, 128.0f, w));
    CHECK(r.setChunks({}).code == Err::NotFound);
    CHECK(r.draw(d.ctx.Get(), nullptr, nullptr, vp, w, 0, waves, nullptr).code == Err::NotFound);
    shaders::ShaderSection empty;
    gamefs::GameFiles noFiles;
    CHECK(r.init(nullptr, empty, noFiles).code == Err::BadLayout);
    CHECK(r.init(d.dev.Get(), empty, noFiles).code == Err::NotFound);   // the WaveShader blobs are not in the section
}

C5R_TEST(game_wave_draw) {
    const std::string exe = c5r::test::exePath();
    const std::string root = c5r::test::gameDir();
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    gamefs::GameFiles files;
    CHECK_OK(files.open(widen(root)));
    Dev d;
    CHECK(d.create());
    WaveRenderer r;
    gamefs::GameFiles noFiles;
    CHECK(r.init(d.dev.Get(), sec, noFiles).code == Err::NotFound);   // the texture is missing
    CHECK_OK(r.init(d.dev.Get(), sec, files));

    // the install's WaveSettings.xml through the game's file system
    std::vector<uint8_t> xmlBytes;
    CHECK_OK(files.read("WaveSettings.xml", xmlBytes));
    WaveObject w;
    CHECK_OK(waveObjectFromSettings(std::string(xmlBytes.begin(), xmlBytes.end()), 128.0f, w));
    std::printf("WaveSettings.xml (%s): pages %u x %u (%u in use), distance %g -> %g, cycle %g s, fades %g-%g / %g-%g, "
                "delay %g-%g, tint %g %g %g\n", files.origin("WaveSettings.xml").c_str(), w.pagesU, w.pagesV, w.pagesInUse,
                w.startDistance, w.endDistance, w.cycleTime, w.fadeInStart, w.fadeInEnd, w.fadeOutStart, w.fadeOutEnd,
                w.restartDelayMin, w.restartDelayMax, w.tint[0], w.tint[1], w.tint[2]);
    {   // the frame with an identity ViewProj and plane z 11.1167, for the comparison with a capture (water-system.md 12)
        float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        uint8_t b[kWaveFrameBytes];
        CHECK_OK(computeWaveFrame(id, w, 10.116731643676758f + 1.0f, b));   // the Tiny save's water height (P8)
        const std::string path = c5r::test::workDir() + "/wave_frame_identity.bin";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            CHECK_EQ(std::fwrite(b, 1, sizeof b, f), sizeof b);
            std::fclose(f);
            std::printf("  wrote %s\n", path.c_str());
        }
    }

    {   // the install's settings with seed 0 (RandomSeed): the first three restarts of a frame. 3601's captures show the
        // same: (0.25, 0.5) holds 201-232 of 248 waves per chunk, (0.5, 0) the next most, and -4.490 is the lowest age
        // (water-system.md 12) [L].
        std::vector<WaveRecord> r3(3);
        for (WaveRecord& rec : r3) rec.age = w.cycleTime + 1.0f;
        CHECK_OK(updateWaves(w, 0.0f, r3));
        std::printf("first restarts of a frame, seed 0: (%g, %g) %.4f; (%g, %g) %.4f; (%g, %g) %.4f\n", r3[0].u, r3[0].v, r3[0].age,
                    r3[1].u, r3[1].v, r3[1].age, r3[2].u, r3[2].v, r3[2].age);
        CHECK(r3[0].u == 0.25f && r3[0].v == 0.5f && r3[1].u == 0.5f && r3[1].v == 0.0f && r3[2].u == 0.75f && r3[2].v == 0.5f);
        CHECK(std::fabs(r3[1].age + 4.490f) < 0.0005f);
    }

    // chunk checks
    std::vector<WaveChunk> bad(1, quadChunk(0, 0, 0, 1));
    bad[0].vb.pop_back();
    CHECK(r.setChunks(bad).code == Err::BadLayout);
    bad[0] = quadChunk(0, 0, 0, 1);
    bad[0].ib.resize(10);
    CHECK(r.setChunks(bad).code == Err::BadLayout);
    bad[0] = quadChunk(0, 0, 0, 1);
    bad[0].ib[0] = 4;   // index 4 of 4 vertices
    CHECK(r.setChunks(bad).code == Err::BadLayout);
    bad[0] = quadChunk(0, 0, 0, 0);
    CHECK(r.setChunks(bad).code == Err::BadLayout);
    bad[0] = quadChunk(0, 0, 0, kWavesPerChunk + 1);
    CHECK(r.setChunks(bad).code == Err::BadLayout);

    // one wave at the centre of a 64 x 64 target: ViewProj maps world x, y in [-32, 32] to clip space
    std::vector<WaveChunk> chunks(1, quadChunk(0, 0, 0, 1));
    CHECK_OK(r.setChunks(chunks));
    std::vector<WaveRecord> waves(1);
    waves[0].age = 0.5f * (w.fadeInEnd + w.fadeOutStart);   // fully visible
    const float vp[16] = {1 / 32.0f, 0, 0, 0, 0, 1 / 32.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1};
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt, st;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &rt)));
    ComPtr<ID3D11RenderTargetView> rtv;
    CHECK(SUCCEEDED(d.dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv)));
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, nullptr, &st)));
    const float black[4] = {0, 0, 0, 0};
    d.ctx->ClearRenderTargetView(rtv.Get(), black);
    D3D11_VIEWPORT viewport = {0, 0, 64, 64, 0, 1};
    d.ctx->RSSetViewports(1, &viewport);
    CHECK(r.draw(d.ctx.Get(), nullptr, nullptr, vp, w, 0, waves, nullptr).code == Err::BadLayout);
    std::vector<WaveRecord> none;
    CHECK(r.draw(d.ctx.Get(), rtv.Get(), nullptr, vp, w, 0, none, nullptr).code == Err::BadLayout);   // waves past the array
    auto readSum = [&](std::vector<uint8_t>& px) {
        d.ctx->CopyResource(st.Get(), rt.Get());
        D3D11_MAPPED_SUBRESOURCE m = {};
        px.assign(64 * 64 * 4, 0);
        if (SUCCEEDED(d.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            for (int y = 0; y < 64; ++y) std::memcpy(&px[y * 256], static_cast<uint8_t*>(m.pData) + y * m.RowPitch, 256);
            d.ctx->Unmap(st.Get(), 0);
        }
    };
    WaveStats stats;
    CHECK_OK(r.draw(d.ctx.Get(), rtv.Get(), nullptr, vp, w, 0, waves, &stats));
    CHECK(stats.chunksDrawn == 1 && stats.indices == 6);
    std::vector<uint8_t> one, two;
    readSum(one);
    // the quad's half size at this age: lerp(start, end, age / cycle) * 128 world units = that / 32 * 32 pixels
    const float t = waves[0].age / w.cycleTime;
    const float halfSize = (w.startDistance + (w.endDistance - w.startDistance) * t) * 128.0f;
    size_t inside = 0, lit = 0, outsideLit = 0;
    uint32_t sum1 = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const float wx = (x + 0.5f - 32.0f), wy = (32.0f - (y + 0.5f));   // world units (1 pixel = 1 unit)
            const bool in = std::fabs(wx) < halfSize - 1 && std::fabs(wy) < halfSize - 1;
            const bool out = std::fabs(wx) > halfSize + 1 || std::fabs(wy) > halfSize + 1;
            const uint8_t* p = &one[(y * 64 + x) * 4];
            const bool any = p[0] || p[1] || p[2];
            if (in) { ++inside; if (any) ++lit; }
            if (out && any) ++outsideLit;
            sum1 += p[0] + p[1] + p[2];
            CHECK(p[3] == 0 || p[3] == 255);   // alpha 1 where drawn
        }
    std::printf("one wave, half size %.2f px: %zu of %zu inner pixels lit, %zu lit outside, colour sum %u\n", halfSize, lit, inside,
                outsideLit, sum1);
    CHECK(inside > 0 && lit > 0);
    CHECK_EQ(outsideLit, static_cast<size_t>(0));
    // additive: a second draw adds the same again
    CHECK_OK(r.draw(d.ctx.Get(), rtv.Get(), nullptr, vp, w, 0, waves, nullptr));
    readSum(two);
    uint32_t sum2 = 0;
    bool neverLess = true;
    for (size_t i = 0; i < two.size(); ++i) {
        if ((i & 3) != 3) sum2 += two[i];
        if (two[i] < one[i]) neverLess = false;
    }
    CHECK(neverLess);
    CHECK(sum2 >= 2 * sum1 - 64 * 64 * 3 && sum2 > sum1);   // twice, up to rounding and the 255 clamp
    // a wave waiting to restart (negative age) is invisible: the fade in is 0
    waves[0].age = -1.0f;
    d.ctx->ClearRenderTargetView(rtv.Get(), black);
    CHECK_OK(r.draw(d.ctx.Get(), rtv.Get(), nullptr, vp, w, 0, waves, nullptr));
    readSum(one);
    uint32_t sum3 = 0;
    for (size_t i = 0; i < one.size(); ++i)
        if ((i & 3) != 3) sum3 += one[i];
    CHECK_EQ(sum3, 0u);
}

C5R_TEST_MAIN
