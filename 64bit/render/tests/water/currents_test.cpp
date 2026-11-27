// c5r::water currents tests (issue 3602): CurrentsFrame, the CPU model, the parameter and input checks, and the GPU
// path with the EXE's WorldCurrents pass 3 (C5R_EXE; exit 77 = SKIP without it) against the model.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "c5r/shaders/shadersection.h"
#include "c5r/water/currents.h"
#include "testkit.h"

using Microsoft::WRL::ComPtr;
using namespace c5r;
using namespace c5r::water;

namespace {

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

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

float f32(const uint8_t* b, int i) {
    float v;
    std::memcpy(&v, b + 4 * i, 4);
    return v;
}

CurrentsParams tiny() {
    CurrentsParams p;
    p.sizeX = 56;
    p.sizeY = 40;
    return p;
}

// GPU texels against the quantised model: share equal, histogram of |difference| per channel (max over channels).
struct Cmp {
    size_t texels = 0, equal = 0, le1 = 0, le4 = 0;
    size_t up[4] = {0, 0, 0, 0}, down[4] = {0, 0, 0, 0};   // per channel: GPU above / below the model
    int maxDiff = 0;
};
Cmp compare(const std::vector<uint8_t>& gpu, const std::vector<uint8_t>& model) {
    Cmp c;
    c.texels = gpu.size() / 4;
    for (size_t i = 0; i < c.texels; ++i) {
        int d = 0;
        for (int k = 0; k < 4; ++k) {
            const int sd = static_cast<int>(gpu[i * 4 + k]) - static_cast<int>(model[i * 4 + k]);
            if (sd > 0) ++c.up[k];
            if (sd < 0) ++c.down[k];
            const int x = std::abs(sd);
            if (x > d) d = x;
        }
        if (d == 0) ++c.equal;
        if (d <= 1) ++c.le1;
        if (d <= 4) ++c.le4;
        if (d > c.maxDiff) c.maxDiff = d;
    }
    return c;
}

void report(const char* what, const Cmp& c) {
    std::printf("%s: %zu texels, equal %zu (%.4f %%), |d| <= 1 %zu, |d| <= 4 %zu, max |d| %d\n", what, c.texels, c.equal,
                100.0 * static_cast<double>(c.equal) / static_cast<double>(c.texels), c.le1, c.le4, c.maxDiff);
    std::printf("  GPU above / below the model per channel: r %zu / %zu, g %zu / %zu, b %zu / %zu, a %zu / %zu\n", c.up[0],
                c.down[0], c.up[1], c.down[1], c.up[2], c.down[2], c.up[3], c.down[3]);
}

// A smooth source: red = 0.5 + 0.25 sin(2 pi u * 3) cos(2 pi v * 2), as an R32_FLOAT texture and as floats.
void makeSource(uint32_t w, uint32_t h, std::vector<float>& img) {
    img.resize(static_cast<size_t>(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const double u = (x + 0.5) / w, v = (y + 0.5) / h;
            img[static_cast<size_t>(y) * w + x] =
                static_cast<float>(0.5 + 0.25 * std::sin(6.283185307179586 * u * 3) * std::cos(6.283185307179586 * v * 2));
        }
}

} // namespace

// ---------------------------------------------------------------- CurrentsFrame and parameters

C5R_TEST(currents_frame_layout) {
    uint8_t b[kCurrentsFrameBytes];
    std::memset(b, 0xcd, sizeof b);
    CurrentsParams p = tiny();
    p.noiseAmplitude = 0.25f;
    p.noiseCell = 0.5f;
    p.gradientScale = 3.0f;
    CHECK_OK(computeCurrentsFrame(p, b));
    CHECK_EQ(f32(b, 0), 56.0f);
    CHECK_EQ(f32(b, 1), 40.0f);
    CHECK_EQ(f32(b, 4), 0.25f);
    CHECK_EQ(f32(b, 8), 0.5f);
    CHECK_EQ(f32(b, 12), 3.0f);
    for (int i : {2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15}) CHECK_EQ(f32(b, i), 0.0f);
}

C5R_TEST(currents_frame_rejects_bad_params) {
    uint8_t b[kCurrentsFrameBytes];
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    CHECK(computeCurrentsFrame(tiny(), nullptr).code == Err::BadLayout);
    CurrentsParams p = tiny();
    p.sizeX = nan;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.gradientScale = inf;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.noiseAmplitude = nan;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.sizeY = 0;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.noiseCell = 0;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.noiseCell = -0.1f;
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
    p = tiny();
    p.noiseCell = 100.0f;   // 56 / 100 < 1: a lattice without a cell would divide by zero in the shader
    CHECK(computeCurrentsFrame(p, b).code == Err::BadLayout);
}

C5R_TEST(currents_params_from_grid) {
    heightfield::HeightGrid g;
    CurrentsParams p;
    CHECK(currentsParamsFromGrid(g, p).code == Err::BadLayout);   // no plots
    g.mapW = 56;
    g.mapH = 0;
    CHECK(currentsParamsFromGrid(g, p).code == Err::BadLayout);
    g.mapH = 40;
    CHECK_OK(currentsParamsFromGrid(g, p));
    CHECK_EQ(p.sizeX, 56.0f);
    CHECK_EQ(p.sizeY, 40.0f);
    CHECK_EQ(p.noiseAmplitude, 0.1f);
    CHECK_EQ(p.noiseCell, 2.0f);
    CHECK_EQ(p.gradientScale, 0.1f);
}

// ---------------------------------------------------------------- the CPU model

C5R_TEST(currents_model_noise_only) {
    std::vector<float> m;
    CHECK_OK(currentsModel(tiny(), nullptr, 0, 0, m));
    CHECK_EQ(m.size(), static_cast<size_t>(256 * 256 * 4));
    // noise only: z = 0, w = 1, x and y near 0.5 (amplitude 0.1: within 0.5 +- 0.4 whatever the lattice gives)
    double sumX = 0;
    bool varies = false;
    for (size_t i = 0; i < m.size(); i += 4) {
        CHECK(m[i + 2] == 0.0f);
        CHECK(m[i + 3] == 1.0f);
        CHECK(std::fabs(m[i] - 0.5f) <= 0.4f);
        CHECK(std::fabs(m[i + 1] - 0.5f) <= 0.4f);
        sumX += m[i];
        if (m[i] != m[0]) varies = true;
    }
    CHECK(varies);
    std::printf("noise-only model mean x %.4f\n", sumX / (256.0 * 256.0));
    // a constant source has no gradient: the same map as no source
    std::vector<float> c(64 * 64, 0.7f), m2;
    CHECK_OK(currentsModel(tiny(), c.data(), 64, 64, m2));
    CHECK_EQ(m2.size(), m.size());
    float worst = 0;   // bilinear weights of a constant leave rounding-level gradients only
    for (size_t i = 0; i < m.size() && i < m2.size(); ++i) worst = std::fmax(worst, std::fabs(m2[i] - m[i]));
    CHECK(worst < 1e-5f);
    // the amplitude scales the noise around 0.5
    CurrentsParams p = tiny();
    p.noiseAmplitude = 0.0f;
    CHECK_OK(currentsModel(p, nullptr, 0, 0, m2));
    for (size_t i = 0; i < m2.size(); i += 4) CHECK(m2[i] == 0.5f && m2[i + 1] == 0.5f);
}

C5R_TEST(currents_model_ramp_flow) {
    // a ramp in u (red = u on a 256-wide source, size 256, step one texel): Sobel gx = -8 / 256, gy = 0; with scale 32
    // g = (-1, 0), |g| = 1: the result is the flow colour alone, (0.5 - 0.5 gy, 0.5 + 0.5 gx, 0, |g|) = (0.5, 0, 0, 1).
    std::vector<float> src(256 * 256);
    for (uint32_t y = 0; y < 256; ++y)
        for (uint32_t x = 0; x < 256; ++x) src[y * 256 + x] = (x + 0.5f) / 256.0f;
    CurrentsParams p;
    p.sizeX = p.sizeY = 256;
    p.gradientScale = 32.0f;
    std::vector<float> m;
    CHECK_OK(currentsModel(p, src.data(), 256, 256, m));
    // away from the wrap seam (x in 2..253)
    const float* o = &m[(128 * 256 + 100) * 4];
    CHECK(std::fabs(o[0] - 0.5f) < 1e-5f);
    CHECK(std::fabs(o[1] - 0.0f) < 1e-5f);
    CHECK(o[2] == 0.0f);
    CHECK(std::fabs(o[3] - 1.0f) < 1e-5f);
}

C5R_TEST(currents_model_rejects_bad_input) {
    std::vector<float> m;
    std::vector<float> src(16, 0.5f);
    CHECK(currentsModel(tiny(), src.data(), 0, 4, m).code == Err::BadLayout);   // size 0
    src[5] = std::numeric_limits<float>::quiet_NaN();                            // NaN height
    CHECK(currentsModel(tiny(), src.data(), 4, 4, m).code == Err::BadLayout);
    src[5] = std::numeric_limits<float>::infinity();
    CHECK(currentsModel(tiny(), src.data(), 4, 4, m).code == Err::BadLayout);
    CurrentsParams p = tiny();
    p.sizeX = -1;
    CHECK(currentsModel(p, nullptr, 0, 0, m).code == Err::BadLayout);
    CHECK(m.empty());
}

C5R_TEST(currents_quantise) {
    std::vector<float> in = {0.0f, 1.0f, 0.5f, -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f / 255.0f, 0.5f / 255.0f};
    std::vector<uint8_t> out;
    quantiseUnorm8(in, out);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 255);
    CHECK_EQ(out[2], 127);   // 127.5: a tie rounds down
    CHECK_EQ(out[3], 0);
    CHECK_EQ(out[4], 255);
    CHECK_EQ(out[5], 0);
    CHECK_EQ(out[6], 1);
    CHECK_EQ(out[7], 0);     // 0.5: a tie rounds down
}

// ---------------------------------------------------------------- the object without the EXE

C5R_TEST(currents_object_state_checks) {
    Currents c;
    Dev d;
    CHECK(d.create());
    CHECK(c.compute(d.ctx.Get(), tiny(), nullptr).code == Err::NotFound);   // init not done
    std::vector<uint8_t> px;
    CHECK(c.readback(d.ctx.Get(), px).code == Err::NotFound);
    CHECK(c.srv() == nullptr && c.texture() == nullptr && c.generation() == 0);
    shaders::ShaderSection empty;   // never opened: no blobs
    CHECK(c.init(nullptr, empty).code == Err::BadLayout);
    CHECK(c.init(d.dev.Get(), empty).code == Err::NotFound);   // the WorldCurrents blobs are not in the section
    CHECK(c.srv() == nullptr);
}

// ---------------------------------------------------------------- with the EXE's program

C5R_TEST(game_currents_gpu_matches_model) {
    const std::string exe = c5r::test::exePath();
    shaders::ShaderSection sec;
    CHECK_OK(sec.open(widen(exe)));
    Dev d;
    CHECK(d.create());
    Currents c;
    CHECK_OK(c.init(d.dev.Get(), sec));
    CHECK(c.srv() != nullptr);
    CHECK(c.compute(nullptr, tiny(), nullptr).code == Err::BadLayout);
    CurrentsParams bad = tiny();
    bad.noiseCell = 0;
    CHECK(c.compute(d.ctx.Get(), bad, nullptr).code == Err::BadLayout);
    CHECK_EQ(c.generation(), 0u);
    // noise only (t0 a null view), at the Tiny and the huge grid sizes
    const float sizes[2][2] = {{56, 40}, {128, 84}};
    for (const auto& s : sizes) {
        CurrentsParams p;
        p.sizeX = s[0];
        p.sizeY = s[1];
        CHECK_OK(c.compute(d.ctx.Get(), p, nullptr));
        std::vector<uint8_t> gpu, model8;
        CHECK_OK(c.readback(d.ctx.Get(), gpu));
        std::vector<float> model;
        CHECK_OK(currentsModel(p, nullptr, 0, 0, model));
        quantiseUnorm8(model, model8);
        const Cmp r = compare(gpu, model8);
        {   // where the GPU stores one step below: the fraction of model * 255 above its integer part
            float lo = 2, hi = -1;
            for (size_t i = 0; i < gpu.size(); ++i)
                if (gpu[i] < model8[i]) {
                    const float t = model[i] * 255.0f - std::floor(model[i] * 255.0f);
                    lo = std::fmin(lo, t);
                    hi = std::fmax(hi, t);
                }
            std::printf("  GPU-below texels: frac(model * 255) in [%.6f, %.6f]\n", lo, hi);
        }
        char what[64];
        std::snprintf(what, sizeof what, "noise only, size %.0f x %.0f", s[0], s[1]);
        report(what, r);
        CHECK(r.texels == 65536);
        CHECK(r.le1 == r.texels);   // every texel within one step of 255 (the rest: rounding at the quantisation)
        // for the comparison with a stock capture (water-system.md 8.6): the GPU map as raw RGBA8 rows in C5R_WORK
        char name[96];
        std::snprintf(name, sizeof name, "/currents_gpu_%.0fx%.0f.rgba8", s[0], s[1]);
        const std::string path = c5r::test::workDir() + name;
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            CHECK_EQ(std::fwrite(gpu.data(), 1, gpu.size(), f), gpu.size());
            std::fclose(f);
            std::printf("  wrote %s\n", path.c_str());
        }
    }
    CHECK_EQ(c.generation(), 2u);
    // with a source: an R32_FLOAT texture
    std::vector<float> img;
    const uint32_t W = 56, H = 40;
    makeSource(W, H, img);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W;
    td.Height = H;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {img.data(), W * 4, 0};
    ComPtr<ID3D11Texture2D> t;
    CHECK(SUCCEEDED(d.dev->CreateTexture2D(&td, &sd, &t)));
    ComPtr<ID3D11ShaderResourceView> v;
    CHECK(SUCCEEDED(d.dev->CreateShaderResourceView(t.Get(), nullptr, &v)));
    CurrentsParams p = tiny();
    CHECK_OK(c.compute(d.ctx.Get(), p, v.Get()));
    std::vector<uint8_t> gpu, model8;
    CHECK_OK(c.readback(d.ctx.Get(), gpu));
    std::vector<float> model;
    CHECK_OK(currentsModel(p, img.data(), W, H, model));
    quantiseUnorm8(model, model8);
    const Cmp r = compare(gpu, model8);
    report("with a 56 x 40 source", r);
    CHECK(r.le1 * 1000 >= r.texels * 999);   // the GPU's bilinear weights are fixed point: allow 0.1 % beyond one step
}
C5R_TEST_MAIN
