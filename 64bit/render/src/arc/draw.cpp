// draw.cpp - c5r::arc::Renderer: the arc's two draws with the EXE's ArrowShader (ticket 0199). Own code, written from the
// behaviour described in research/02-exe-internals/arrows-and-movement-path.md 1.7 and the states of 0179's capture in
// research/05-graphics-composition/arrows-and-path-line.md 3 (D83). The program is read from the player's EXE at run
// time; nothing of it is in this file.
#include <c5r/arc/arc.h>

#include <windows.h>
#include <d3d11.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "c5r/shaders/shadersection.h"

namespace c5r {
namespace arc {

namespace {

template <class T> void rel(T*& p)
{
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const char* what)
{
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string(what) + b);
}

} // namespace

void packConstants(const float viewProj[16], const Settings& s, float out[24])
{
    std::memset(out, 0, 24 * sizeof(float));
    std::memcpy(out, viewProj, 16 * sizeof(float));
    out[16] = s.alpha;
    out[20] = s.ambient;
}

PassState passState(uint32_t pass)
{
    PassState p;
    p.blendEnable = pass != 0;
    if (pass == 0) {
        // the pre-pass: blend off (the D3D11 defaults ONE / ZERO / ADD), colour write mask alpha only
        p.srcBlend = D3D11_BLEND_ONE;
        p.destBlend = D3D11_BLEND_ZERO;
        p.blendOp = D3D11_BLEND_OP_ADD;
        p.srcBlendAlpha = D3D11_BLEND_ONE;
        p.destBlendAlpha = D3D11_BLEND_ZERO;
        p.blendOpAlpha = D3D11_BLEND_OP_ADD;
        p.writeMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
    } else {
        p.srcBlend = D3D11_BLEND_SRC_ALPHA;
        p.destBlend = D3D11_BLEND_INV_SRC_ALPHA;
        p.blendOp = D3D11_BLEND_OP_ADD;
        p.srcBlendAlpha = D3D11_BLEND_ONE;
        p.destBlendAlpha = D3D11_BLEND_ONE;
        p.blendOpAlpha = D3D11_BLEND_OP_ADD;
        p.writeMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    p.depthEnable = true;
    p.depthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    p.depthFunc = D3D11_COMPARISON_LESS_EQUAL;
    p.stencilEnable = false;
    p.fillMode = D3D11_FILL_SOLID;
    p.cullMode = D3D11_CULL_FRONT;
    p.frontCounterClockwise = true;
    p.depthClipEnable = true;
    p.scissorEnable = false;
    p.multisampleEnable = false;
    p.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    return p;
}

struct Renderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11BlendState* blend[2] = {};
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    const void* lastArrow = nullptr;
    uint64_t lastGen = 0;
    uint32_t indexCount = 0;
    ~Impl()
    {
        rel(vs); rel(ps); rel(layout); rel(vb); rel(ib); rel(cb); rel(blend[0]); rel(blend[1]); rel(depth); rel(raster);
        rel(dev);
    }
};

Renderer::~Renderer() { delete m_impl; }

Status Renderer::init(ID3D11Device* dev, const std::wstring& exePath)
{
    delete m_impl;
    m_impl = nullptr;
    if (!dev) return Status::fail(Err::BadLayout, "arc::Renderer::init: no device");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    auto blob = [&](uint32_t va, shaders::Stage st, const char* what, const shaders::ShaderBlob** out) -> Status {
        char v[16];
        std::snprintf(v, sizeof v, "0x%08x", va);
        const shaders::ShaderBlob* b = sec.findByVa(va);
        if (!b) return Status::fail(Err::NotFound, std::string(what) + " blob " + v + " not in the .shader section");
        if (b->stage != st) return Status::fail(Err::BadLayout, std::string(what) + " blob " + v + " has another stage");
        *out = b;
        return Status::Ok();
    };
    const shaders::ShaderBlob *bv = nullptr, *bp = nullptr;
    C5R_TRY(blob(kArrowVsVa, shaders::Stage::Vertex, "ArrowShader VS", &bv));
    C5R_TRY(blob(kArrowPsVa, shaders::Stage::Pixel, "ArrowShader PS", &bp));
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    C5R_TRY(hrOk(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (ArrowShader)"));
    C5R_TRY(hrOk(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (ArrowShader)"));
    const D3D11_INPUT_ELEMENT_DESC el[5] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    C5R_TRY(hrOk(dev->CreateInputLayout(el, 5, bv->bytes, bv->size, &p->layout), "CreateInputLayout (ArrowShader)"));
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = kMaxVertices * static_cast<UINT>(sizeof(Vertex));
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &p->vb), "CreateBuffer (arc vertices)"));
        d.ByteWidth = kMaxIndices * 4u;
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &p->ib), "CreateBuffer (arc indices)"));
        d.ByteWidth = kConstantBytes;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        C5R_TRY(hrOk(dev->CreateBuffer(&d, nullptr, &p->cb), "CreateBuffer (ArrowShader constants)"));
    }
    for (uint32_t i = 0; i < 2; ++i) {
        const PassState ps = passState(i);
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = ps.blendEnable ? TRUE : FALSE;
        b.RenderTarget[0].SrcBlend = static_cast<D3D11_BLEND>(ps.srcBlend);
        b.RenderTarget[0].DestBlend = static_cast<D3D11_BLEND>(ps.destBlend);
        b.RenderTarget[0].BlendOp = static_cast<D3D11_BLEND_OP>(ps.blendOp);
        b.RenderTarget[0].SrcBlendAlpha = static_cast<D3D11_BLEND>(ps.srcBlendAlpha);
        b.RenderTarget[0].DestBlendAlpha = static_cast<D3D11_BLEND>(ps.destBlendAlpha);
        b.RenderTarget[0].BlendOpAlpha = static_cast<D3D11_BLEND_OP>(ps.blendOpAlpha);
        b.RenderTarget[0].RenderTargetWriteMask = static_cast<UINT8>(ps.writeMask);
        C5R_TRY(hrOk(dev->CreateBlendState(&b, &p->blend[i]), "CreateBlendState (arc)"));
    }
    {
        const PassState ps = passState(1);
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable = ps.depthEnable ? TRUE : FALSE;
        d.DepthWriteMask = static_cast<D3D11_DEPTH_WRITE_MASK>(ps.depthWriteMask);
        d.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(ps.depthFunc);
        d.StencilEnable = ps.stencilEnable ? TRUE : FALSE;
        d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        d.BackFace = d.FrontFace;
        C5R_TRY(hrOk(dev->CreateDepthStencilState(&d, &p->depth), "CreateDepthStencilState (arc)"));
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = static_cast<D3D11_FILL_MODE>(ps.fillMode);
        r.CullMode = static_cast<D3D11_CULL_MODE>(ps.cullMode);
        r.FrontCounterClockwise = ps.frontCounterClockwise ? TRUE : FALSE;
        r.DepthClipEnable = ps.depthClipEnable ? TRUE : FALSE;
        r.ScissorEnable = ps.scissorEnable ? TRUE : FALSE;
        r.MultisampleEnable = ps.multisampleEnable ? TRUE : FALSE;
        C5R_TRY(hrOk(dev->CreateRasterizerState(&r, &p->raster), "CreateRasterizerState (arc)"));
    }
    m_impl = p.release();
    return Status::Ok();
}

namespace {

Status upload(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const void* data, size_t bytes, const char* what)
{
    D3D11_MAPPED_SUBRESOURCE m = {};
    C5R_TRY(hrOk(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), what));
    std::memcpy(m.pData, data, bytes);
    ctx->Unmap(buf, 0);
    return Status::Ok();
}

Status checkMesh(const Mesh& m)
{
    if (m.vertices.size() > kMaxVertices || m.indices.size() > kMaxIndices)
        return Status::fail(Err::BadLayout, "arc mesh larger than the buffers (" + std::to_string(m.vertices.size()) + " vertices, " +
                                                std::to_string(m.indices.size()) + " indices)");
    if (m.indices.size() % 3 != 0) return Status::fail(Err::BadLayout, "arc mesh: index count not a multiple of 3");
    for (uint32_t i : m.indices)
        if (i >= m.vertices.size()) return Status::fail(Err::BadLayout, "arc mesh: an index past the vertices");
    return Status::Ok();
}

} // namespace

Status Renderer::draw(ID3D11DeviceContext* ctx, const DrawInput& in, const Arrow& arrow, DrawStats* stats)
{
    if (!m_impl) return Status::fail(Err::NotFound, "arc::Renderer::draw: init not done");
    if (stats) *stats = DrawStats();
    if (!arrow.visible()) return Status::Ok();
    const bool changed = m_impl->lastArrow != &arrow || m_impl->lastGen != arrow.generation();
    C5R_TRY(drawImpl(ctx, in, arrow.mesh(), arrow.settings(), changed, stats));
    m_impl->lastArrow = &arrow;
    m_impl->lastGen = arrow.generation();
    return Status::Ok();
}

Status Renderer::drawMesh(ID3D11DeviceContext* ctx, const DrawInput& in, const Mesh& mesh, const Settings& s, DrawStats* stats)
{
    if (!m_impl) return Status::fail(Err::NotFound, "arc::Renderer::drawMesh: init not done");
    if (stats) *stats = DrawStats();
    m_impl->lastArrow = nullptr;
    return drawImpl(ctx, in, mesh, s, true, stats);
}

Status Renderer::drawImpl(ID3D11DeviceContext* ctx, const DrawInput& in, const Mesh& mesh, const Settings& s, bool doUpload,
                          DrawStats* stats)
{
    Impl* p = m_impl;
    if (!ctx || !in.colour || !in.depth) return Status::fail(Err::BadLayout, "arc draw: no context, colour target or depth");
    if (in.width == 0 || in.height == 0) return Status::fail(Err::BadLayout, "arc draw: viewport 0");
    for (float v : in.viewProj)
        if (!std::isfinite(v)) return Status::fail(Err::BadLayout, "arc draw: viewProj not finite");
    if (!std::isfinite(s.alpha) || !std::isfinite(s.ambient)) return Status::fail(Err::BadLayout, "arc draw: alpha or ambient not finite");
    C5R_TRY(checkMesh(mesh));
    if (mesh.indices.empty()) return Status::Ok();
    if (doUpload) {
        p->indexCount = 0;
        C5R_TRY(upload(ctx, p->vb, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex), "Map (arc vertices)"));
        C5R_TRY(upload(ctx, p->ib, mesh.indices.data(), mesh.indices.size() * 4u, "Map (arc indices)"));
        p->indexCount = static_cast<uint32_t>(mesh.indices.size());
        if (stats) ++stats->uploads;
    }
    if (p->indexCount == 0) return Status::fail(Err::BadLayout, "arc draw: no mesh uploaded");
    float cb[24];
    packConstants(in.viewProj, s, cb);
    C5R_TRY(upload(ctx, p->cb, cb, sizeof cb, "Map (ArrowShader constants)"));

    const UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetInputLayout(p->layout);
    ctx->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
    ctx->IASetIndexBuffer(p->ib, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p->vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(p->ps, nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, &p->cb);
    ctx->PSSetConstantBuffers(1, 1, &p->cb);
    ctx->OMSetRenderTargets(1, &in.colour, in.depth);
    ctx->OMSetDepthStencilState(p->depth, 0);
    ctx->RSSetState(p->raster);
    D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    const float factor[4] = {0, 0, 0, 0};
    for (uint32_t pass = 0; pass < 2; ++pass) {
        ctx->OMSetBlendState(p->blend[pass], factor, 0xFFFFFFFFu);
        ctx->DrawIndexed(p->indexCount, 0, 0);
        if (stats) {
            ++stats->draws;
            stats->indices += p->indexCount;
        }
    }
    return Status::Ok();
}

} // namespace arc
} // namespace c5r
