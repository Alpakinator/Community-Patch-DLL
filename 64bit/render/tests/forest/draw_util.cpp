// Helpers of the c5r::forest draw tests (ticket 4551). Own code; the device, target and texture helpers follow
// render/tests/seamg/ice_util.cpp (issue 4306) of this repository.
#include "draw_util.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include "testkit.h"

namespace c5r { namespace ftest {

using namespace c5r::forest;

template <class T> static void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

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
    const char* e = std::getenv("C5R_FOREST_REF");
    const std::string d = e && *e ? e : "F:/c5-64bit-work/0301/3907/exports";
    if (GetFileAttributesW(wide(d + "/units_land/L01/draws.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "the 3907 catalogue exports are not at " + d +
                                           " (no units_land/L01/draws.json): set C5R_FOREST_REF to the exports folder, e.g. "
                                           "set C5R_FOREST_REF=F:/c5-64bit-work/0301/3907/exports (D137; never skipped)");
        return std::string();
    }
    return d;
}

bool readRef(const std::string& ex, const std::string& rel, std::vector<uint8_t>& out, size_t expectSize) {
    const std::string d = refDir();
    if (d.empty()) return false;
    if (!readFile(d + "/" + ex + "/" + rel, out)) {
        test::fail(__FILE__, __LINE__, "reference file missing: " + d + "/" + ex + "/" + rel);
        return false;
    }
    if (expectSize && out.size() != expectSize) {
        test::fail(__FILE__, __LINE__, "reference file " + ex + "/" + rel + " has " + std::to_string(out.size()) +
                                           " bytes, want " + std::to_string(expectSize));
        return false;
    }
    return true;
}

uint32_t ExportRef::views() const {
    uint32_t v = 0;
    for (const DrawRef& d : draws) v = std::max(v, d.view + 1);
    return v;
}

static std::string manifestPath() {
    const char* e = std::getenv("C5R_FOREST_MANIFEST");
    if (e && *e) return e;
    std::string f = __FILE__;   // <repo>/render/tests/forest/draw_util.cpp
    std::replace(f.begin(), f.end(), '\\', '/');
    const size_t at = f.rfind("/render/tests/forest/");
    if (at == std::string::npos) return std::string();
    return f.substr(0, at) + "/render/evidence/0431/4551/manifest.txt";
}

const std::vector<ExportRef>& manifest() {
    static std::vector<ExportRef>* m = nullptr;
    if (m) return *m;
    m = new std::vector<ExportRef>();
    const std::string path = manifestPath();
    std::vector<uint8_t> bytes;
    if (path.empty() || !readFile(path, bytes)) {
        test::fail(__FILE__, __LINE__, "the forest test manifest is not at '" + path +
                                           "': set C5R_FOREST_MANIFEST to render/evidence/0431/4551/manifest.txt");
        return *m;
    }
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        if (kw == "export") {
            ExportRef e;
            size_t n = 0;
            ls >> e.name >> n;
            m->push_back(e);
        } else if (kw == "shaders" && !m->empty()) {
            ls >> m->back().vsFile >> m->back().psFile;
        } else if (kw == "draw" && !m->empty()) {
            DrawRef d;
            std::string k;
            ls >> d.n >> k >> d.view >> k >> d.cb1 >> k >> d.cb7 >> k >> d.vb >> d.vbBytes >> k >> d.ib >> d.ibBytes >> k >>
                d.indexCount;
            for (int i = 0; i < 9; ++i) {
                TexRef& t = d.t[i];
                ls >> k >> t.file >> t.format >> t.width >> t.height >> t.depth >> t.mips;
            }
            if (!ls) {
                test::fail(__FILE__, __LINE__, "manifest line not readable: " + line.substr(0, 80));
                continue;
            }
            m->back().draws.push_back(d);
        }
    }
    if (m->empty()) test::fail(__FILE__, __LINE__, "the forest test manifest " + path + " lists no export");
    return *m;
}

const ExportRef* findExport(const std::string& name) {
    for (const ExportRef& e : manifest())
        if (e.name == name) return &e;
    test::fail(__FILE__, __LINE__, "export " + name + " not in the manifest");
    return nullptr;
}

float f32(const std::vector<uint8_t>& b, size_t off) {
    float v = 0;
    if (off + 4 <= b.size()) std::memcpy(&v, b.data() + off, 4);
    return v;
}

uint32_t ulpDistance(float a, float b) {
    int32_t x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    if ((x < 0) != (y < 0)) return a == b ? 0u : 0xFFFFFFFFu;
    const int64_t d = static_cast<int64_t>(x) - static_cast<int64_t>(y);
    return static_cast<uint32_t>(d < 0 ? -d : d);
}

uint32_t maskedDiff(const uint8_t* a, const uint8_t* b, uint32_t ulps, uint32_t* first, uint32_t* ulp14y) {
    uint32_t n = 0;
    float fa, fb;
    std::memcpy(&fa, a + 0xE4, 4);
    std::memcpy(&fb, b + 0xE4, 4);
    const uint32_t u = ulpDistance(fa, fb);
    if (ulp14y) *ulp14y = u;
    if (first) *first = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < kFrameBytes; ++i) {
        if (!forestFrameStored(i) || a[i] == b[i]) continue;
        if (i >= 0xE4 && i < 0xE8 && u <= ulps) continue;
        if (first && *first == 0xFFFFFFFFu) *first = i;
        ++n;
    }
    return n;
}

FrameInputs capturedInputs(const std::vector<std::vector<uint8_t>>& blocks) {
    FrameInputs in;
    in.numViews = static_cast<uint32_t>(blocks.size());
    for (uint32_t i = 0; i < 16; ++i) in.proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    for (uint32_t v = 0; v < blocks.size() && v < kMaxViews; ++v) {
        const std::vector<uint8_t>& b = blocks[v];
        View& w = in.views[v];
        for (int i = 0; i < 16; ++i) {
            w.view[i] = f32(b, 4 * i);
            w.overlayProj[i] = f32(b, 0x40 + 4 * i);
        }
        w.haveCamPos = true;
        for (int c = 0; c < 3; ++c) w.camPos[c] = f32(b, 0xC0 + 4 * c);
    }
    const std::vector<uint8_t>& b = blocks.front();
    for (int c = 0; c < 4; ++c) in.fowAnim[c] = f32(b, 0x80 + 4 * c);
    in.width = static_cast<uint32_t>(f32(b, 0x90));
    in.height = static_cast<uint32_t>(f32(b, 0x94));
    in.overlayAlpha = f32(b, 0xA0);
    for (int c = 0; c < 3; ++c) in.auxDir[c] = f32(b, 0xB0 + 4 * c);
    for (int c = 0; c < 4; ++c) in.fog.color[c] = f32(b, 0xD0 + 4 * c);
    in.fog.density = -f32(b, 0xE0);
    in.fog.w = f32(b, 0xEC);
    return in;
}

// ---- device and targets ----

Device::~Device() {
    rel(ctx);
    rel(dev);
}

Status Device::create() {
    const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    const HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev,
                                        nullptr, &ctx);
    if (FAILED(h)) return Status::fail(Err::Device, "D3D11CreateDevice (hardware, 11_0) failed");
    return Status::Ok();
}

Target::~Target() {
    rel(dsv);
    rel(depth);
    rel(rtv);
    rel(staging);
    rel(tex);
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

void Target::clear(ID3D11DeviceContext* ctx, const float rgba[4]) {
    ctx->ClearRenderTargetView(rtv, rgba);
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

// bytes per texel, or per 4x4 block when *block
static uint32_t sizeOf(DXGI_FORMAT f, bool* block) {
    *block = false;
    switch (f) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: *block = true; return 8;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: *block = true; return 16;
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: *block = true; return 16;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return 4;
    case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_R8_UNORM: return 1;
    default: return 0;
    }
}

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
    s.MiscFlags = 0;
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

Status makeTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::string& ex, const TexRef& t, bool genMips,
                   bool oneMip, ID3D11ShaderResourceView** srv) {
    std::vector<uint8_t> data;
    if (!readRef(ex, t.file, data)) return Status::fail(Err::NotFound, t.file);
    const DXGI_FORMAT f = static_cast<DXGI_FORMAT>(t.format);
    bool block = false;
    const uint32_t sz = sizeOf(f, &block);
    if (!sz) return Status::fail(Err::Unsupported, t.file + ": format " + std::to_string(t.format));
    if (t.depth > 1) {
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = t.width;
        d.Height = t.height;
        d.Depth = t.depth;
        d.MipLevels = 1;
        d.Format = f;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const size_t need = static_cast<size_t>(t.width) * t.height * t.depth * sz;
        if (data.size() < need) return Status::fail(Err::Truncated, t.file + ": 3D data short");
        D3D11_SUBRESOURCE_DATA sd = {data.data(), t.width * sz, t.width * t.height * sz};
        ID3D11Texture3D* x = nullptr;
        if (FAILED(dev->CreateTexture3D(&d, &sd, &x))) return Status::fail(Err::Device, t.file + ": CreateTexture3D");
        const HRESULT h = dev->CreateShaderResourceView(x, nullptr, srv);
        x->Release();
        return SUCCEEDED(h) ? Status::Ok() : Status::fail(Err::Device, t.file + ": CreateShaderResourceView");
    }
    if (genMips || oneMip) {
        if (block) return Status::fail(Err::Unsupported, t.file + ": generated mips need an uncompressed format");
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = t.width;
        d.Height = t.height;
        d.MipLevels = oneMip ? 1 : t.mips;
        d.ArraySize = 1;
        d.Format = f;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = oneMip ? 0 : D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (data.size() < static_cast<size_t>(t.width) * t.height * sz) return Status::fail(Err::Truncated, t.file + ": mip 0 short");
        ID3D11Texture2D* x = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &x))) return Status::fail(Err::Device, t.file + ": CreateTexture2D");
        ctx->UpdateSubresource(x, 0, nullptr, data.data(), t.width * sz, 0);
        const HRESULT h = dev->CreateShaderResourceView(x, nullptr, srv);
        x->Release();
        if (FAILED(h)) return Status::fail(Err::Device, t.file + ": CreateShaderResourceView");
        if (!oneMip) ctx->GenerateMips(*srv);
        return Status::Ok();
    }
    std::vector<D3D11_SUBRESOURCE_DATA> init(t.mips);
    size_t off = 0;
    for (uint32_t m = 0; m < t.mips; ++m) {
        const uint32_t mw = (t.width >> m) ? (t.width >> m) : 1, mh = (t.height >> m) ? (t.height >> m) : 1;
        const uint32_t pitch = block ? ((mw + 3) / 4) * sz : mw * sz, rows = block ? (mh + 3) / 4 : mh;
        if (off + static_cast<size_t>(pitch) * rows > data.size()) return Status::fail(Err::Truncated, t.file + ": data short");
        init[m] = D3D11_SUBRESOURCE_DATA{data.data() + off, pitch, pitch * rows};
        off += static_cast<size_t>(pitch) * rows;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = t.width;
    d.Height = t.height;
    d.MipLevels = t.mips;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* x = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, init.data(), &x))) return Status::fail(Err::Device, t.file + ": CreateTexture2D");
    const HRESULT h = dev->CreateShaderResourceView(x, nullptr, srv);
    x->Release();
    return SUCCEEDED(h) ? Status::Ok() : Status::fail(Err::Device, t.file + ": CreateShaderResourceView");
}

Shared::~Shared() {
    for (auto& x : v) rel(x);
}

bool Shared::make(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::string& ex, const DrawRef& d, bool t3OneMip) {
    for (uint32_t s = 2; s < 9; ++s) {
        if (!d.t[s].bound()) continue;
        const bool target = s == 2 || s == 3 || s == 6;   // render targets with generated mips (frame-start contents)
        const Status st = makeTexture(dev, ctx, ex, d.t[s], target && !(s == 3 && t3OneMip), s == 3 && t3OneMip, &v[s]);
        CHECK_OK(st);
        if (!st.ok()) return false;
    }
    return true;
}

void Shared::into(FrameInputs& in, uint32_t entry) const {
    in.terrainOverlay = v[2];
    in.screenOverlay = v[3];
    in.fogRamp = v[4];
    in.fowMask = v[6];
    in.cloudBase = v[7];
    const uint32_t li = lutIndex(entry);
    if (li < kLuts) {
        in.colorKeyLut[li] = v[5];
        in.fowLut[li] = v[8];
    }
}

// ---- the plain replay ----

Replay::~Replay() {
    rel(vs_);
    rel(ps_);
    rel(il_);
    rel(cb1_);
    rel(cb7_);
    for (auto& s : samp_) rel(s);
    rel(rs_);
    rel(ds_);
    rel(bs_);
}

bool Replay::init(ID3D11Device* dev, const ExportRef& ex) {
    dev_ = dev;
    export_ = ex.name;
    std::vector<uint8_t> vs, ps;
    if (!readRef(ex.name, ex.vsFile, vs, 1100) || !readRef(ex.name, ex.psFile, ps, 2264)) return false;
    CHECK(SUCCEEDED(dev->CreateVertexShader(vs.data(), vs.size(), nullptr, &vs_)));
    CHECK(SUCCEEDED(dev->CreatePixelShader(ps.data(), ps.size(), nullptr, &ps_)));
    // the layout as the capture's state.json lists it (ia.layout of every tree draw)
    const D3D11_INPUT_ELEMENT_DESC il[4] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    CHECK(SUCCEEDED(dev->CreateInputLayout(il, 4, vs.data(), vs.size(), &il_)));
    D3D11_BUFFER_DESC b = {};
    b.Usage = D3D11_USAGE_DYNAMIC;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    b.ByteWidth = kFrameBytes;
    CHECK(SUCCEEDED(dev->CreateBuffer(&b, nullptr, &cb1_)));
    b.ByteWidth = kBlockBytes;
    CHECK(SUCCEEDED(dev->CreateBuffer(&b, nullptr, &cb7_)));
    // the capture's samplers (state.json PS samp: s0-s3, s6, s7 wrap; s4, s5, s8 clamp; MIN_MAG_MIP_LINEAR, bias 0,
    // MaxAnisotropy 0 as recorded (the runtime takes it as 1 for a linear filter), NEVER, LOD 0..FLT_MAX)
    for (uint32_t i = 0; i < 9; ++i) {
        D3D11_SAMPLER_DESC d = {};
        d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        const D3D11_TEXTURE_ADDRESS_MODE a = (i == 4 || i == 5 || i == 8) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        d.AddressU = d.AddressV = d.AddressW = a;
        d.MaxAnisotropy = 0;
        d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        CHECK(SUCCEEDED(dev->CreateSamplerState(&d, &samp_[i])));
    }
    D3D11_RASTERIZER_DESC r = {};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_BACK;
    r.FrontCounterClockwise = TRUE;
    r.DepthClipEnable = TRUE;
    CHECK(SUCCEEDED(dev->CreateRasterizerState(&r, &rs_)));
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ds.FrontFace.StencilFailOp = ds.FrontFace.StencilDepthFailOp = ds.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    ds.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    ds.BackFace = ds.FrontFace;
    CHECK(SUCCEEDED(dev->CreateDepthStencilState(&ds, &ds_)));
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    CHECK(SUCCEEDED(dev->CreateBlendState(&bd, &bs_)));
    return vs_ && ps_ && il_ && cb1_ && cb7_ && rs_ && ds_ && bs_;
}

bool Replay::draw(ID3D11DeviceContext* ctx, const Target& t, const DrawRef& d, const Shared& sh) {
    std::vector<uint8_t> cb1, cb7, vbB, ibB;
    if (!readRef(export_, d.cb1, cb1, kFrameBytes) || !readRef(export_, d.cb7, cb7, kBlockBytes) ||
        !readRef(export_, d.vb, vbB, d.vbBytes) || !readRef(export_, d.ib, ibB, d.ibBytes))
        return false;
    ID3D11ShaderResourceView* t0 = nullptr;
    ID3D11ShaderResourceView* t1 = nullptr;
    Status st = makeTexture(dev_, ctx, export_, d.t[0], false, false, &t0);
    CHECK_OK(st);
    st = makeTexture(dev_, ctx, export_, d.t[1], false, false, &t1);
    CHECK_OK(st);
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = static_cast<UINT>(vbB.size());
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {vbB.data(), 0, 0};
    CHECK(SUCCEEDED(dev_->CreateBuffer(&bd, &sd, &vb)));
    bd.ByteWidth = static_cast<UINT>(ibB.size());
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = ibB.data();
    CHECK(SUCCEEDED(dev_->CreateBuffer(&bd, &sd, &ib)));
    const bool ok = t0 && t1 && vb && ib;
    if (ok) {
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(cb1_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        std::memcpy(m.pData, cb1.data(), kFrameBytes);
        ctx->Unmap(cb1_, 0);
        ctx->Map(cb7_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        std::memcpy(m.pData, cb7.data(), kBlockBytes);
        ctx->Unmap(cb7_, 0);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(t.w), static_cast<float>(t.h), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(rs_);
        ctx->OMSetRenderTargets(1, &t.rtv, t.dsv);
        ctx->OMSetDepthStencilState(ds_, 0);
        ctx->OMSetBlendState(bs_, nullptr, 0xFFFFFFFFu);
        ctx->IASetInputLayout(il_);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const UINT stride = kStride, off = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
        ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->VSSetShader(vs_, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(ps_, nullptr, 0);
        ID3D11Buffer* cbs[8] = {nullptr, cb1_, nullptr, nullptr, nullptr, nullptr, nullptr, cb7_};
        ctx->VSSetConstantBuffers(0, 8, cbs);
        ctx->PSSetConstantBuffers(0, 8, cbs);
        ctx->PSSetSamplers(0, 9, samp_);
        ID3D11ShaderResourceView* srv[9] = {t0, t1, sh.v[2], sh.v[3], sh.v[4], sh.v[5], sh.v[6], sh.v[7], sh.v[8]};
        ctx->PSSetShaderResources(0, 9, srv);
        ctx->DrawIndexedInstanced(d.indexCount, 1, 0, 0, 0);
        ID3D11ShaderResourceView* none[9] = {};
        ctx->PSSetShaderResources(0, 9, none);
    }
    rel(t0);
    rel(t1);
    rel(vb);
    rel(ib);
    return ok;
}

DiffStats diffStats(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const uint8_t clear[4],
                    std::vector<uint8_t>* diffImage) {
    DiffStats s;
    std::vector<uint32_t> hist(256, 0);
    const size_t n = std::min(a.size(), b.size()) / 4;
    if (diffImage) diffImage->assign(n * 4, 0);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* pa = &a[i * 4];
        const uint8_t* pb = &b[i * 4];
        const bool drawn = std::memcmp(pa, clear, 4) != 0 || std::memcmp(pb, clear, 4) != 0;
        if (!drawn) continue;
        ++s.drawn;
        uint32_t d = 0;
        for (int c = 0; c < 4; ++c) d = std::max<uint32_t>(d, static_cast<uint32_t>(std::abs(int(pa[c]) - int(pb[c]))));
        ++hist[d];
        if (d) ++s.differing;
        s.max = std::max(s.max, d);
        if (diffImage) {
            const uint8_t v = static_cast<uint8_t>(std::min<uint32_t>(255, d * 16));
            (*diffImage)[i * 4 + 0] = v;
            (*diffImage)[i * 4 + 1] = v;
            (*diffImage)[i * 4 + 2] = v;
            (*diffImage)[i * 4 + 3] = 255;
        }
    }
    uint64_t acc = 0;
    bool haveMed = false;
    for (uint32_t d = 0; d < 256; ++d) {
        acc += hist[d];
        if (!haveMed && acc * 2 >= s.drawn && s.drawn) {
            s.median = d;
            haveMed = true;
        }
        if (acc * 100 >= s.drawn * 95 && s.drawn) {
            s.p95 = d;
            break;
        }
    }
    return s;
}

}} // namespace c5r::ftest
