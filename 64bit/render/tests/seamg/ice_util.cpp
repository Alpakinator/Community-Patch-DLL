// Shared helpers of the c5r::ice tests (issue 4306). Own code.
#include "ice_util.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "testkit.h"

namespace c5r { namespace itest {

using namespace c5r::ice;

std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

std::string refDir() {
    const char* e = std::getenv("C5R_ICE_REF");
    std::string d = e && *e ? e : "F:/c5-64bit-work/0169/4301/ref";
    const DWORD a = GetFileAttributesW(wide(d + "/ice_probe.json").c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "the reference set of task 4301 is not at " + d +
                                           " (no ice_probe.json): set C5R_ICE_REF to its ref/ folder, e.g. "
                                           "set C5R_ICE_REF=F:/c5-64bit-work/0169/4301/ref (D137; never skipped)");
        return std::string();
    }
    return d;
}

bool readRef(const std::string& rel, std::vector<uint8_t>& out, size_t expectSize) {
    const std::string d = refDir();
    if (d.empty()) return false;
    if (!readFile(d + "/" + rel, out)) {
        test::fail(__FILE__, __LINE__, "reference file missing: " + d + "/" + rel);
        return false;
    }
    if (expectSize && out.size() != expectSize) {
        test::fail(__FILE__, __LINE__, "reference file " + rel + " has " + std::to_string(out.size()) + " bytes, want " +
                                           std::to_string(expectSize));
        return false;
    }
    return true;
}

const CapturedDraw* capturedDraws(size_t* n) {
    static const CapturedDraw k[] = {
        {"load1 draw 171 (scene)", "stock_load1/buffers/51172_9b6b504e.bin", true},
        {"load1 draw 86 (reflection)", "stock_load1/buffers/51180_70d5a20c.bin", false},
        {"load2 draw 191 (scene)", "stock_load2/buffers/51178_79458cee.bin", true},
        {"load2 draw 88 (reflection)", "stock_load2/buffers/51170_81013a40.bin", false},
    };
    *n = sizeof k / sizeof k[0];
    return k;
}

float f32(const std::vector<uint8_t>& b, size_t off) {
    float v = 0;
    if (off + 4 <= b.size()) std::memcpy(&v, b.data() + off, 4);
    return v;
}
uint32_t u32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= b.size()) std::memcpy(&v, b.data() + off, 4);
    return v;
}

uint32_t maskedDiff(const uint8_t* a, const uint8_t* b, uint32_t* first) {
    uint32_t n = 0;
    if (first) *first = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < kIceFrameBytes; ++i)
        if (iceFrameStored(i) && a[i] != b[i]) {
            if (first && *first == 0xFFFFFFFFu) *first = i;
            ++n;
        }
    return n;
}

bool tinyFields(const std::vector<uint8_t>& scene, IceFields& out) {
    out = IceFields();
    std::vector<uint8_t> js;
    if (!readRef("ice_probe.json", js)) return false;
    const std::string s(js.begin(), js.end());
    const std::string key = "\"tail_hex\": \"";
    const size_t at = s.find(key);   // the first record: stock-load1
    if (at == std::string::npos || at + key.size() + 512 > s.size()) {
        test::fail(__FILE__, __LINE__, "ice_probe.json: no tail_hex of 256 bytes");
        return false;
    }
    for (size_t i = 0; i < 256; ++i) {
        const std::string h = s.substr(at + key.size() + 2 * i, 2);
        out.raw[i] = static_cast<uint8_t>(std::strtoul(h.c_str(), nullptr, 16));
    }
    if (scene.size() != kIceFrameBytes) return false;
    auto row = [&](uint32_t r, uint32_t c) { return f32(scene, r * 16 + c * 4); };
    // +0xFF80.. by the row map (polar-ice.md 7)
    out.set(field::kHeightAbove, out.get(0xFF18));                       // 25: the probe's +0xFF18 copy
    out.set(field::kDepthBelow, out.get(field::kWaterHeight) - row(7, 0)); // 8
    out.set(field::kTextureRepeat, row(17, 0));
    out.set(field::kBumpRepeat, row(18, 0));
    out.set(field::kSpecularPower, row(19, 0));
    for (uint32_t c = 0; c < 2; ++c) {
        out.set(field::kSideTexRepeat + 4 * c, row(22, c));
        out.set(field::kSideBumpRepeat + 4 * c, row(23, c));
    }
    for (uint32_t c = 0; c < 3; ++c) {
        out.set(field::kSubsurface + 4 * c, row(20, c));
        out.set(field::kUnderwater1 + 4 * c, row(24, c));
        out.set(field::kUnderwater2 + 4 * c, row(25, c));
    }
    out.set(field::kColorRamp, row(26, 0));
    out.set(field::kAlphaRamp, row(27, 0));
    out.valid = true;
    return true;
}

FrameInputs capturedInputs(const std::vector<uint8_t>& frame, const std::vector<uint8_t>& scene) {
    FrameInputs in;
    for (int i = 0; i < 16; ++i) {
        in.cam.view[i] = f32(frame, 4 * i);
        in.cam.proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        in.overlayProj[i] = f32(frame, 0x80 + 4 * i);
    }
    in.cam.width = 2554;
    in.cam.height = 1411;
    in.haveCamPos = true;
    for (int c = 0; c < 3; ++c) in.camPos[c] = f32(frame, 0xC0 + 4 * c);
    for (int c = 0; c < 4; ++c) in.fowAnim[c] = f32(frame, 0xD0 + 4 * c);
    in.haveFogCamZ = true;
    in.fogCamZ = f32(scene, 0xC8);
    in.fog.w = f32(frame, 0x1DC);
    in.worldWidth = kTinyWorldWidth;
    return in;
}

// ---- device and targets ----

Device::~Device() {
    if (ctx) ctx->Release();
    if (dev) dev->Release();
}

Status Device::create() {
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    const HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev,
                                        nullptr, &ctx);
    if (FAILED(h)) return Status::fail(Err::Device, "D3D11CreateDevice (hardware, 11_0) failed");
    return Status::Ok();
}

Target::~Target() {
    if (dsv) dsv->Release();
    if (depth) depth->Release();
    if (rtv) rtv->Release();
    if (staging) staging->Release();
    if (tex) tex->Release();
}

Status Target::create(ID3D11Device* dev, uint32_t width, uint32_t height) {
    w = width;
    h = height;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex))) return Status::fail(Err::Device, "CreateTexture2D (target)");
    if (FAILED(dev->CreateRenderTargetView(tex, nullptr, &rtv))) return Status::fail(Err::Device, "CreateRenderTargetView");
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging))) return Status::fail(Err::Device, "CreateTexture2D (staging)");
    d.Usage = D3D11_USAGE_DEFAULT;
    d.CPUAccessFlags = 0;
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &depth))) return Status::fail(Err::Device, "CreateTexture2D (depth)");
    if (FAILED(dev->CreateDepthStencilView(depth, nullptr, &dsv))) return Status::fail(Err::Device, "CreateDepthStencilView");
    return Status::Ok();
}

void Target::clear(ID3D11DeviceContext* ctx) {
    const float zero[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv, zero);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

Status Target::read(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba) {
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return Status::fail(Err::Device, "Map (staging)");
    rgba.resize(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, w * 4);
    ctx->Unmap(staging, 0);
    return Status::Ok();
}

namespace {
// bytes per texel, or per 4x4 block when *block
uint32_t sizeOf(DXGI_FORMAT f, bool* block) {
    *block = false;
    switch (f) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: *block = true; return 8;
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: *block = true; return 16;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
    case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_R8_UNORM: return 1;
    default: return 0;
    }
}
} // namespace

Status readTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, std::vector<uint8_t>& out) {
    out.clear();
    if (!srv) return Status::fail(Err::NotFound, "readTexture: no view");
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    ID3D11Texture2D* t2 = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t2)))) {
        res->Release();
        return Status::fail(Err::Unsupported, "readTexture: not a 2D texture");
    }
    res->Release();
    D3D11_TEXTURE2D_DESC d;
    t2->GetDesc(&d);
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &st))) {
        t2->Release();
        return Status::fail(Err::Device, "readTexture: CreateTexture2D (staging)");
    }
    ctx->CopyResource(st, t2);
    t2->Release();
    bool block = false;
    const uint32_t sz = sizeOf(d.Format, &block);
    if (!sz) {
        st->Release();
        return Status::fail(Err::Unsupported, "readTexture: format not handled");
    }
    for (uint32_t a = 0; a < d.ArraySize; ++a)
        for (uint32_t m = 0; m < d.MipLevels; ++m) {
            const uint32_t w = (d.Width >> m) ? (d.Width >> m) : 1, h = (d.Height >> m) ? (d.Height >> m) : 1;
            const uint32_t rows = block ? (h + 3) / 4 : h, rowBytes = block ? ((w + 3) / 4) * sz : w * sz;
            D3D11_MAPPED_SUBRESOURCE map;
            const UINT sub = D3D11CalcSubresource(m, a, d.MipLevels);
            if (FAILED(ctx->Map(st, sub, D3D11_MAP_READ, 0, &map))) {
                st->Release();
                return Status::fail(Err::Device, "readTexture: Map");
            }
            for (uint32_t y = 0; y < rows; ++y) {
                const uint8_t* p = static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
                out.insert(out.end(), p, p + rowBytes);
            }
            ctx->Unmap(st, sub);
        }
    st->Release();
    return Status::Ok();
}

bool writeBmp(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    std::vector<uint8_t> b(54 + static_cast<size_t>(w) * h * 4, 0);
    auto p32 = [&](size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); };
    b[0] = 'B';
    b[1] = 'M';
    p32(2, static_cast<uint32_t>(b.size()));
    p32(10, 54);
    p32(14, 40);
    p32(18, w);
    p32(22, static_cast<uint32_t>(-static_cast<int32_t>(h)));  // top-down
    b[26] = 1;
    b[28] = 32;
    p32(34, static_cast<uint32_t>(static_cast<size_t>(w) * h * 4));
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        b[54 + i * 4 + 0] = rgba[i * 4 + 2];
        b[54 + i * 4 + 1] = rgba[i * 4 + 1];
        b[54 + i * 4 + 2] = rgba[i * 4 + 0];
        b[54 + i * 4 + 3] = 255;
    }
    return writeFile(path, b);
}

// ---- the frame's shared inputs t7-t10 (moved from ice_draw_test.cpp by issue 4310 for the 8-chunk case) ----

// A 2D texture from tightly packed mips (uncompressed `bpp` bytes per texel, or BC with `block` bytes per 4x4 block).
Status makeTex2D(ID3D11Device* dev, const std::vector<uint8_t>& data, uint32_t w, uint32_t h, uint32_t mips, DXGI_FORMAT f,
                 uint32_t bpp, uint32_t block, ID3D11ShaderResourceView** srv) {
    std::vector<D3D11_SUBRESOURCE_DATA> init(mips);
    size_t off = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        const uint32_t mw = (w >> m) ? (w >> m) : 1, mh = (h >> m) ? (h >> m) : 1;
        const uint32_t pitch = block ? ((mw + 3) / 4) * block : mw * bpp, rows = block ? (mh + 3) / 4 : mh;
        if (off + static_cast<size_t>(pitch) * rows > data.size()) return Status::fail(Err::Truncated, "makeTex2D: data short");
        init[m] = D3D11_SUBRESOURCE_DATA{data.data() + off, pitch, pitch * rows};
        off += static_cast<size_t>(pitch) * rows;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* t = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, init.data(), &t))) return Status::fail(Err::Device, "makeTex2D: CreateTexture2D");
    const HRESULT h2 = dev->CreateShaderResourceView(t, nullptr, srv);
    t->Release();
    return SUCCEEDED(h2) ? Status::Ok() : Status::fail(Err::Device, "makeTex2D: CreateShaderResourceView");
}

// t7-t10 of draw 171 from the capture: the fog ramp (128 x 8, 8 mips), the FOW mask (1024^2 RGBA8; mip 0 of the frame
// start, its mips generated: the capture holds the target before draws 90-97 redraw it), the cloud base (1024^2 BC1,
// 11 mips), the TerrainFOW LUT (32^3).
Shared::~Shared() {
    for (auto& x : v)
        if (x) {
            x->Release();
            x = nullptr;
        }
}

bool Shared::make(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    std::vector<uint8_t> ramp, mask, cloud, lut;
    if (!readRef("stock_load1/textures/2455_f9ed79fe.bin", ramp, 5500)) return false;
    if (!readRef("stock_load1/textures/5427_0c123d45.bin", mask)) return false;
    if (!readRef("stock_load1/textures/5448_676602f7.bin", cloud, 699064)) return false;
    if (!readRef("stock_load1/textures/2473_49245115.bin", lut, 131072)) return false;
    Status st = makeTex2D(dev, ramp, 128, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 0, &v[0]);
    CHECK_OK(st);
    if (!st.ok()) return false;
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = 1024;
        d.MipLevels = 11;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D* t = nullptr;
        CHECK(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)));
        if (!t) return false;
        ctx->UpdateSubresource(t, 0, nullptr, mask.data(), 4096, 0);
        CHECK(SUCCEEDED(dev->CreateShaderResourceView(t, nullptr, &v[1])));
        t->Release();
        if (!v[1]) return false;
        ctx->GenerateMips(v[1]);
    }
    st = makeTex2D(dev, cloud, 1024, 1024, 11, DXGI_FORMAT_BC1_UNORM, 0, 8, &v[2]);
    CHECK_OK(st);
    if (!st.ok()) return false;
    D3D11_TEXTURE3D_DESC d = {};
    d.Width = d.Height = d.Depth = 32;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {lut.data(), 128, 4096};
    ID3D11Texture3D* t3 = nullptr;
    CHECK(SUCCEEDED(dev->CreateTexture3D(&d, &sd, &t3)));
    if (!t3) return false;
    CHECK(SUCCEEDED(dev->CreateShaderResourceView(t3, nullptr, &v[3])));
    t3->Release();
    return v[3] != nullptr;
}

void Shared::into(FrameInputs& in) const {
    in.fogRamp = v[0];
    in.fowMask = v[1];
    in.cloudBase = v[2];
    in.fowLut = v[3];
}

}} // namespace c5r::itest
