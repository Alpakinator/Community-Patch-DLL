// render/tools/quadtest/quadtest_core.cpp (task 2906). Own code; Windows SDK only (d3d11, dxgi, d3dcompiler, WIC).
#include "quadtest_core.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace quadtest {

namespace {
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
Status dev(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return Status::Ok();
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s failed, hr=0x%08lx", what, static_cast<unsigned long>(hr));
    return Status::fail(Err::Device, buf);
}

struct Vertex { float pos[4]; float nrm[3]; float uv[2]; }; // 36 bytes: POSITION, NORMAL, TEXCOORD0

const char* kRefHlsl =
    "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "struct V { float4 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; };\n"
    "struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "cbuffer C : register(b0) { row_major float4x4 M; };\n"
    "O vs(V v) { O o; o.p = mul(float4(v.p.xyz, 1), M); o.uv = v.uv; return o; }\n"
    "float4 ps(O i) : SV_Target { return t0.Sample(s0, i.uv); }\n";

// Common draw: the caller supplies the two shaders' bytecode and the constant buffer bytes.
Status drawWith(Device& d, const void* vsb, size_t vsn, const void* psb, size_t psn, const std::vector<uint8_t>& cbBytes,
                ID3D11ShaderResourceView* srv, const DrawParams& p, Image& out) {
    if (!srv) return Status::fail(Err::BadLayout, "no texture view");
    const terrainsets::SamplerStateDesc* ss = terrainsets::samplerState(p.samplerStateId);
    if (!ss) return Status::fail(Err::NotFound, "sampler state " + std::to_string(p.samplerStateId));
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11InputLayout* il = nullptr;
    ID3D11Buffer* vb = nullptr; ID3D11Buffer* cb = nullptr; ID3D11SamplerState* smp = nullptr;
    ID3D11Texture2D* rt = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11Texture2D* st = nullptr;
    Status s = Status::Ok();
    do {
        if (!(s = dev(d.dev->CreateVertexShader(vsb, vsn, nullptr, &vs), "CreateVertexShader"))) break;
        if (!(s = dev(d.dev->CreatePixelShader(psb, psn, nullptr, &ps), "CreatePixelShader"))) break;
        const D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        if (!(s = dev(d.dev->CreateInputLayout(el, 3, vsb, vsn, &il), "CreateInputLayout"))) break;
        const float a = p.quadMin, b = p.quadMax;
        const Vertex v[4] = {{{a, b, 0.5f, 1}, {0, 0, 1}, {0, 0}}, {{b, b, 0.5f, 1}, {0, 0, 1}, {1, 0}},
                             {{a, a, 0.5f, 1}, {0, 0, 1}, {0, 1}}, {{b, a, 0.5f, 1}, {0, 0, 1}, {1, 1}}};
        D3D11_BUFFER_DESC bd = {sizeof v, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA sd = {v, 0, 0};
        if (!(s = dev(d.dev->CreateBuffer(&bd, &sd, &vb), "CreateBuffer(vb)"))) break;
        std::vector<uint8_t> cbData(cbBytes);
        cbData.resize((cbData.size() + 15) & ~size_t(15), 0);
        D3D11_BUFFER_DESC cd = {static_cast<UINT>(cbData.size()), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA csd = {cbData.data(), 0, 0};
        if (!(s = dev(d.dev->CreateBuffer(&cd, &csd, &cb), "CreateBuffer(cb)"))) break;
        D3D11_SAMPLER_DESC sm = {};
        sm.Filter = static_cast<D3D11_FILTER>(ss->filter);
        sm.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(ss->address[0]);
        sm.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(ss->address[1]);
        sm.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(ss->address[2]);
        sm.MaxAnisotropy = ss->maxAnisotropy ? ss->maxAnisotropy : 1;
        sm.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(ss->comparison ? ss->comparison : D3D11_COMPARISON_NEVER);
        sm.MaxLOD = D3D11_FLOAT32_MAX;
        if (!(s = dev(d.dev->CreateSamplerState(&sm, &smp), "CreateSamplerState"))) break;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = p.width; td.Height = p.height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (!(s = dev(d.dev->CreateTexture2D(&td, nullptr, &rt), "CreateTexture2D(rt)"))) break;
        if (!(s = dev(d.dev->CreateRenderTargetView(rt, nullptr, &rtv), "CreateRenderTargetView"))) break;
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (!(s = dev(d.dev->CreateTexture2D(&td, nullptr, &st), "CreateTexture2D(staging)"))) break;

        const float clear[4] = {0, 0, 0, 0};
        d.ctx->ClearRenderTargetView(rtv, clear);
        d.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(p.width), static_cast<float>(p.height), 0, 1};
        d.ctx->RSSetViewports(1, &vp);
        UINT stride = sizeof(Vertex), off = 0;
        d.ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
        d.ctx->IASetInputLayout(il);
        d.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        d.ctx->VSSetShader(vs, nullptr, 0);
        d.ctx->VSSetConstantBuffers(0, 1, &cb);
        d.ctx->PSSetShader(ps, nullptr, 0);
        d.ctx->PSSetConstantBuffers(0, 1, &cb);
        d.ctx->PSSetShaderResources(0, 1, &srv);
        d.ctx->PSSetSamplers(0, 1, &smp);
        d.ctx->Draw(4, 0);
        d.ctx->CopyResource(st, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        if (!(s = dev(d.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m), "Map"))) break;
        out.width = p.width; out.height = p.height;
        out.rgba.resize(size_t(p.width) * p.height * 4);
        for (uint32_t y = 0; y < p.height; ++y)
            std::memcpy(&out.rgba[size_t(y) * p.width * 4], static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                        size_t(p.width) * 4);
        d.ctx->Unmap(st, 0);
        d.ctx->ClearState();
    } while (false);
    rel(vs); rel(ps); rel(il); rel(vb); rel(cb); rel(smp); rel(rt); rel(rtv); rel(st);
    return s;
}
} // namespace

Device::~Device() { rel(ctx); rel(dev); }

Status Device::create() {
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) return Status::fail(Err::Device, "no hardware or WARP device");
        adapter = "WARP";
        return Status::Ok();
    }
    IDXGIDevice* dx = nullptr; IDXGIAdapter* ad = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dx))) && SUCCEEDED(dx->GetAdapter(&ad))) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(ad->GetDesc(&desc))) {
            char buf[128];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, buf, sizeof buf, nullptr, nullptr);
            adapter = buf;
        }
    }
    rel(ad); rel(dx);
    return Status::Ok();
}

Status findPass(const shaders::ShaderSection& sec, const char* setName, uint32_t passIndex, GamePass& out) {
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet(setName);
    if (!set) return Status::fail(Err::NotFound, std::string("shader set ") + setName);
    const terrainsets::PassDesc* pd = nullptr;
    for (const auto& p : set->passes) if (p.index == passIndex) pd = &p;
    if (!pd || !pd->sm50[0] || !pd->sm50[1]) return Status::fail(Err::NotFound, "pass without an SM5 VS and PS");
    out.vs = sec.findByVa(pd->sm50[0]);
    out.ps = sec.findByVa(pd->sm50[1]);
    if (!out.vs || !out.ps) return Status::fail(Err::NotFound, "blob VA not in the .shader section");
    if (out.vs->stage != shaders::Stage::Vertex || out.ps->stage != shaders::Stage::Pixel)
        return Status::fail(Err::BadLayout, "blob of the wrong stage");
    out.cb0 = nullptr;
    for (const auto& b : set->cbuffers) if (b.slot == 0) out.cb0 = b.layout;
    if (!out.cb0) return Status::fail(Err::NotFound, "no slot-0 constant buffer in the set");
    return Status::Ok();
}

Status writeMatrix(const terrainsets::CbLayout& layout, const char* member, const float m[16], std::vector<uint8_t>& cb) {
    const terrainsets::CbMember* mem = nullptr;
    for (const auto& x : layout.members) if (std::strcmp(x.name, member) == 0) mem = &x;
    if (!mem) return Status::fail(Err::NotFound, std::string("member ") + member + " not in " + layout.name);
    if (mem->type != terrainsets::MemberType::Float4x4) return Status::fail(Err::BadLayout, "member is not a float4x4");
    if (mem->offset + 64 > layout.sizeBytes) return Status::fail(Err::BadLayout, "member past the buffer end");
    if (cb.size() < layout.sizeBytes) cb.resize(layout.sizeBytes, 0);
    std::memcpy(&cb[mem->offset], m, 64);
    return Status::Ok();
}

Status drawGame(Device& d, const GamePass& pass, ID3D11ShaderResourceView* srv, const DrawParams& p, Image& out) {
    std::vector<uint8_t> cb;
    C5R_TRY(writeMatrix(*pass.cb0, "Transform", p.transform, cb));
    return drawWith(d, pass.vs->bytes, pass.vs->size, pass.ps->bytes, pass.ps->size, cb, srv, p, out);
}

Status drawReference(Device& d, ID3D11ShaderResourceView* srv, const DrawParams& p, Image& out) {
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    size_t n = std::strlen(kRefHlsl);
    Status s = dev(D3DCompile(kRefHlsl, n, "ref", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err), "D3DCompile(vs)");
    rel(err);
    if (s) s = dev(D3DCompile(kRefHlsl, n, "ref", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err), "D3DCompile(ps)");
    rel(err);
    if (s) {
        std::vector<uint8_t> cb(64);
        std::memcpy(cb.data(), p.transform, 64);
        s = drawWith(d, vsb->GetBufferPointer(), vsb->GetBufferSize(), psb->GetBufferPointer(), psb->GetBufferSize(), cb,
                     srv, p, out);
    }
    rel(vsb); rel(psb);
    return s;
}

Status loadGameTexture(Device& d, const std::wstring& gameDir, const std::string& name, ID3D11ShaderResourceView** srv,
                       std::string* origin, std::string* format) {
    gamefs::GameFiles fs;
    C5R_TRY(fs.open(gameDir));
    std::vector<uint8_t> bytes;
    if (!fs.exists(name)) return Status::fail(Err::NotFound, "texture " + name);
    C5R_TRY(fs.read(name, bytes, origin));
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
    if (format) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "DXGI %d, %ux%u, %u mips", static_cast<int>(img.format), img.width, img.height, img.mipLevels);
        *format = buf;
    }
    ID3D11Resource* tex = nullptr;
    Status s = gamefs::createTexture(d.dev, img, &tex, srv);
    rel(tex);
    return s;
}

Status compare(const Image& a, const Image& b, Diff& out) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
        return Status::fail(Err::BadLayout, "image sizes differ");
    out = Diff();
    uint64_t sum = 0, n = 0, over = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4) {
        if (!a.rgba[i + 3] && !b.rgba[i + 3]) continue;
        ++out.covered;
        for (int c = 0; c < 3; ++c) {
            uint32_t dlt = static_cast<uint32_t>(std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
            sum += dlt; ++n;
            if (dlt > out.maxAbs) out.maxAbs = dlt;
            if (dlt > 2) ++over;
        }
    }
    out.meanAbs = n ? double(sum) / double(n) : 0;
    out.fracOver2 = n ? double(over) / double(n) : 0;
    return Status::Ok();
}

Image downscale(const Image& img, uint32_t w, uint32_t h) {
    Image o; o.width = w; o.height = h; o.rgba.assign(size_t(w) * h * 4, 0);
    if (!img.width || !img.height || !w || !h) return o;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t x0 = x * img.width / w, x1 = (x + 1) * img.width / w, y0 = y * img.height / h, y1 = (y + 1) * img.height / h;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            uint32_t acc[4] = {0, 0, 0, 0}, cnt = 0;
            for (uint32_t yy = y0; yy < y1; ++yy)
                for (uint32_t xx = x0; xx < x1; ++xx, ++cnt)
                    for (int c = 0; c < 4; ++c) acc[c] += img.rgba[(size_t(yy) * img.width + xx) * 4 + c];
            for (int c = 0; c < 4; ++c) o.rgba[(size_t(y) * w + x) * 4 + c] = static_cast<uint8_t>(acc[c] / cnt);
        }
    return o;
}

Status writePng(const std::wstring& path, const Image& img) {
    if (!img.width || !img.height || img.rgba.size() != size_t(img.width) * img.height * 4)
        return Status::fail(Err::BadLayout, "image size");
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = nullptr; IWICStream* st = nullptr; IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    Status s = dev(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)), "WIC factory");
    if (s) s = dev(f->CreateStream(&st), "CreateStream");
    if (s && FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) s = Status::fail(Err::Io, "cannot write the PNG");
    if (s) s = dev(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc), "CreateEncoder");
    if (s) s = dev(enc->Initialize(st, WICBitmapEncoderNoCache), "encoder Initialize");
    if (s) s = dev(enc->CreateNewFrame(&fr, nullptr), "CreateNewFrame");
    if (s) s = dev(fr->Initialize(nullptr), "frame Initialize");
    if (s) s = dev(fr->SetSize(img.width, img.height), "SetSize");
    // The PNG encoder takes 32bpp BGRA, not RGBA: swap R and B into a copy.
    std::vector<uint8_t> bgra(img.rgba);
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    if (s) s = dev(fr->SetPixelFormat(&pf), "SetPixelFormat");
    if (s && pf != GUID_WICPixelFormat32bppBGRA) s = Status::fail(Err::Device, "WIC refused 32bpp BGRA");
    if (s) s = dev(fr->WritePixels(img.height, img.width * 4, static_cast<UINT>(bgra.size()), bgra.data()), "WritePixels");
    if (s) s = dev(fr->Commit(), "frame Commit");
    if (s) s = dev(enc->Commit(), "encoder Commit");
    rel(fr); rel(enc); rel(st); rel(f);
    if (SUCCEEDED(co)) CoUninitialize();
    return s;
}

}} // namespace c5r::quadtest
