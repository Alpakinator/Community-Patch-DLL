// draw.cpp - layer 3 of c5r::pathmodels: the path's draws on a D3D11 device with the EXE's UIShader (ticket 0300).
// The states are the ones 3801's stock capture shows for all 54 path draws (arrows-and-path-line.md 4,
// path-models.md 4): back buffer + the scene's depth, blend SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE), depth
// LESS_EQUAL with write, cull back with FrontCounterClockwise, depth clip; three linear-wrap samplers.
#include "c5r/pathmodels/pathmodels.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "c5r/gamefs/dds.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/units/draw.h"   // units::sha256Hex (used read-only)

namespace c5r { namespace pathmodels {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hrFail(HRESULT hr, const char* what) {
    char b[48];
    std::snprintf(b, sizeof b, "hr 0x%08X", static_cast<unsigned>(hr));
    return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + " " + b);
}

}  // namespace

const ProgramDigest& uiShaderVs() {
    static const ProgramDigest d = {"7ce2cf445956e2583f2f729361801d1973ac6315c42216d4c7b619823dd95dac", 0x067FC9E8};
    return d;
}
const ProgramDigest& uiShaderPs() {
    static const ProgramDigest d = {"7d85ec9f760e8e66642397efb69cb6fc57e9cea478071f999f1a5f35abd55806", 0x067FEBA0};
    return d;
}

void frameConstants(const Mat4& cameraWorld, const Mat4& viewProj, float out[32]) {
    std::memcpy(out, cameraWorld.m, sizeof cameraWorld.m);
    std::memcpy(out + 16, viewProj.m, sizeof viewProj.m);
}

struct PathRenderer::Impl {
    std::vector<uint8_t> vsBytes, psBytes;
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Buffer* cb0 = nullptr;
    ID3D11Buffer* cb1 = nullptr;
    struct MeshGpu {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        uint32_t indices = 0;
        ID3D11ShaderResourceView* srv[3] = {nullptr, nullptr, nullptr};   // borrowed from textures
    };
    std::vector<std::vector<MeshGpu>> meshes;   // [asset][mesh]
    std::map<std::string, ID3D11ShaderResourceView*> textures;
    bool ready = false;

    void releaseGpu() {
        for (auto& a : meshes)
            for (auto& m : a) {
                rel(m.vb);
                rel(m.ib);
            }
        meshes.clear();
        for (auto& kv : textures) kv.second->Release();
        textures.clear();
        rel(vs);
        rel(ps);
        rel(layout);
        rel(blend);
        rel(depth);
        rel(raster);
        rel(sampler);
        rel(cb0);
        rel(cb1);
        rel(dev);
        ready = false;
    }
};

PathRenderer::PathRenderer() : p_(new Impl) {}
PathRenderer::~PathRenderer() { release(); }
void PathRenderer::release() {
    if (p_) p_->releaseGpu();
}
bool PathRenderer::ready() const { return p_->ready; }

Status PathRenderer::openProgramsFromBlobs(const std::vector<std::vector<uint8_t>>& blobs) {
    p_->vsBytes.clear();
    p_->psBytes.clear();
    for (const auto& b : blobs) {
        const std::string h = units::sha256Hex(b.data(), b.size());
        if (h == uiShaderVs().sha256) p_->vsBytes = b;
        if (h == uiShaderPs().sha256) p_->psBytes = b;
    }
    std::string missing;
    if (p_->vsBytes.empty()) missing += std::string("UIShader VS ") + uiShaderVs().sha256;
    if (p_->psBytes.empty()) missing += std::string(missing.empty() ? "" : "; ") + "UIShader PS " + uiShaderPs().sha256;
    if (!missing.empty()) {
        p_->vsBytes.clear();
        p_->psBytes.clear();
        return Status::fail(Err::NotFound, "not in the EXE's .shader section: " + missing);
    }
    return Status::Ok();
}

Status PathRenderer::openPrograms(const std::wstring& exePath) {
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(sec.blobs().size());
    for (const auto& b : sec.blobs()) blobs.emplace_back(b.bytes, b.bytes + b.size);   // looked up by digest, not VA
    return openProgramsFromBlobs(blobs);
}

Status PathRenderer::create(ID3D11Device* dev, const PathModels& models, const FileReader& read) {
    Impl& p = *p_;
    p.releaseGpu();
    if (!dev) return Status::fail(Err::Device, "no device");
    if (p.vsBytes.empty() || p.psBytes.empty()) return Status::fail(Err::NotFound, "UIShader programs not opened");
    if (!models.loaded()) return Status::fail(Err::NotFound, "path models not loaded");
    dev->AddRef();
    p.dev = dev;
    Status st = [&]() -> Status {
        HRESULT hr = dev->CreateVertexShader(p.vsBytes.data(), p.vsBytes.size(), nullptr, &p.vs);
        if (FAILED(hr)) return hrFail(hr, "CreateVertexShader (UIShader)");
        hr = dev->CreatePixelShader(p.psBytes.data(), p.psBytes.size(), nullptr, &p.ps);
        if (FAILED(hr)) return hrFail(hr, "CreatePixelShader (UIShader)");
        static const D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = dev->CreateInputLayout(el, 3, p.vsBytes.data(), p.vsBytes.size(), &p.layout);
        if (FAILED(hr)) return hrFail(hr, "CreateInputLayout");
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = dev->CreateBlendState(&bd, &p.blend);
        if (FAILED(hr)) return hrFail(hr, "CreateBlendState");
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = dev->CreateDepthStencilState(&dd, &p.depth);
        if (FAILED(hr)) return hrFail(hr, "CreateDepthStencilState");
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.FrontCounterClockwise = TRUE;
        rd.DepthClipEnable = TRUE;
        hr = dev->CreateRasterizerState(&rd, &p.raster);
        if (FAILED(hr)) return hrFail(hr, "CreateRasterizerState");
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = dev->CreateSamplerState(&sd, &p.sampler);
        if (FAILED(hr)) return hrFail(hr, "CreateSamplerState");
        D3D11_BUFFER_DESC cd = {};
        cd.Usage = D3D11_USAGE_DYNAMIC;
        cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cd.ByteWidth = kCb0Bytes;
        hr = dev->CreateBuffer(&cd, nullptr, &p.cb0);
        if (FAILED(hr)) return hrFail(hr, "CreateBuffer cb0");
        cd.ByteWidth = kCb1Bytes;
        hr = dev->CreateBuffer(&cd, nullptr, &p.cb1);
        if (FAILED(hr)) return hrFail(hr, "CreateBuffer cb1");
        // Textures: every name the materials use, t2 a cube map.
        std::map<std::string, bool> needCube;
        for (uint32_t a = 0; a < kAssetCount; ++a)
            for (const PathMesh& m : models.asset(static_cast<Asset>(a)).meshes)
                for (int k = 0; k < 3; ++k)
                    if (!m.texture[k].empty()) needCube[m.texture[k]] = needCube[m.texture[k]] || k == 2;
        {
            // The stand-in for a material without SREFMap: 1 x 1 black with alpha 1, so the specular term is 0 (an
            // alpha of 0 would make the PS's power 0 and its log(0) x 0 undefined) [I]
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = td.Height = 1;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const uint32_t black = 0xFF000000u;
            D3D11_SUBRESOURCE_DATA sd0 = {&black, 4, 0};
            ID3D11Texture2D* t = nullptr;
            hr = dev->CreateTexture2D(&td, &sd0, &t);
            if (FAILED(hr)) return hrFail(hr, "CreateTexture2D (no-SREF stand-in)");
            ID3D11ShaderResourceView* v = nullptr;
            hr = dev->CreateShaderResourceView(t, nullptr, &v);
            t->Release();
            if (FAILED(hr)) return hrFail(hr, "CreateShaderResourceView (no-SREF stand-in)");
            p.textures[std::string()] = v;   // key "": looked up by an empty t1 name
        }
        for (const auto& kv : needCube) {
            std::vector<uint8_t> bytes;
            Status rs = read(kv.first, bytes);
            if (!rs.ok()) return Status::fail(rs.code, "texture " + kv.first + ": " + rs.msg);
            gamefs::DdsImage img;
            Status ps = gamefs::parseDds(bytes.data(), bytes.size(), img);
            if (!ps.ok()) return Status::fail(ps.code, "texture " + kv.first + ": " + ps.msg);
            if (kv.second && !img.cube)
                return Status::fail(Err::Unsupported, "texture " + kv.first + " is bound as t2 but is not a cube map");
            if (!kv.second && (img.cube || img.depth != 1 || img.arraySize != 1))
                return Status::fail(Err::Unsupported, "texture " + kv.first + " is bound as t0 / t1 but is not 2D");
            ID3D11Resource* tex = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            Status cs = gamefs::createTexture(dev, img, &tex, &srv);
            if (tex) tex->Release();
            if (!cs.ok()) return Status::fail(cs.code, "texture " + kv.first + ": " + cs.msg);
            p.textures[kv.first] = srv;
        }
        p.meshes.resize(kAssetCount);
        for (uint32_t a = 0; a < kAssetCount; ++a) {
            const PathAsset& pa = models.asset(static_cast<Asset>(a));
            for (const PathMesh& m : pa.meshes) {
                Impl::MeshGpu g;
                p.meshes[a].push_back(g);   // pushed first, so releaseGpu() frees partial work
                Impl::MeshGpu& mg = p.meshes[a].back();
                D3D11_BUFFER_DESC bd2 = {};
                bd2.Usage = D3D11_USAGE_IMMUTABLE;
                bd2.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                bd2.ByteWidth = static_cast<UINT>(m.vertices.size());
                D3D11_SUBRESOURCE_DATA sr = {m.vertices.data(), 0, 0};
                hr = dev->CreateBuffer(&bd2, &sr, &mg.vb);
                if (FAILED(hr)) return hrFail(hr, "CreateBuffer VB");
                bd2.BindFlags = D3D11_BIND_INDEX_BUFFER;
                bd2.ByteWidth = static_cast<UINT>(m.indices.size() * sizeof(uint16_t));
                sr.pSysMem = m.indices.data();
                hr = dev->CreateBuffer(&bd2, &sr, &mg.ib);
                if (FAILED(hr)) return hrFail(hr, "CreateBuffer IB");
                mg.indices = static_cast<uint32_t>(m.indices.size());
                for (int k = 0; k < 3; ++k) mg.srv[k] = p.textures[m.texture[k]];
            }
        }
        return Status::Ok();
    }();
    if (!st.ok()) {
        p.releaseGpu();
        return st;
    }
    p.ready = true;
    return Status::Ok();
}

Status PathRenderer::draw(ID3D11DeviceContext* ctx, const std::vector<DrawItem>& items, const float cb1[32]) {
    Impl& p = *p_;
    if (!p.ready || !ctx) return Status::fail(Err::Device, "path renderer not ready");
    for (const DrawItem& d : items) {
        const size_t a = static_cast<size_t>(d.asset);
        if (a >= p.meshes.size() || d.mesh >= p.meshes[a].size())
            return Status::fail(Err::BadLayout, "draw item asset " + std::to_string(a) + " mesh " +
                                                    std::to_string(d.mesh) + " out of range");
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    HRESULT hr = ctx->Map(p.cb1, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    if (FAILED(hr)) return hrFail(hr, "Map cb1");
    std::memcpy(ms.pData, cb1, kCb1Bytes);
    ctx->Unmap(p.cb1, 0);
    ctx->IASetInputLayout(p.layout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->PSSetShader(p.ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = {p.cb0, p.cb1};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ID3D11SamplerState* ss[3] = {p.sampler, p.sampler, p.sampler};
    ctx->PSSetSamplers(0, 3, ss);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(p.blend, bf, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(p.depth, 0);
    ctx->RSSetState(p.raster);
    for (const DrawItem& d : items) {
        const Impl::MeshGpu& g = p.meshes[static_cast<size_t>(d.asset)][d.mesh];
        hr = ctx->Map(p.cb0, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        if (FAILED(hr)) return hrFail(hr, "Map cb0");
        std::memset(ms.pData, 0, kCb0Bytes);
        std::memcpy(ms.pData, d.world.m, sizeof d.world.m);
        ctx->Unmap(p.cb0, 0);
        const UINT stride = kVertexStride, offset = 0;
        ctx->IASetVertexBuffers(0, 1, &g.vb, &stride, &offset);
        ctx->IASetIndexBuffer(g.ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->PSSetShaderResources(0, 3, g.srv);
        ctx->DrawIndexed(g.indices, 0, 0);
    }
    return Status::Ok();
}

}}  // namespace c5r::pathmodels
