// render/src/water/waves.cpp - the coast waves with the EXE's WaveShader (issue 3606). Behaviour and facts:
// c5r/water/waves.h and research/05-graphics-composition/water-system.md section 12. Own code, written from the
// knowledge base's description.
#include "c5r/water/waves.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace water {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hr(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[64];
    std::snprintf(b, sizeof b, " failed (0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, std::string("waves: ") + what + b);
}

uint32_t rdU(const uint8_t* p, size_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
float rdF(const uint8_t* p, size_t off) {
    float v;
    std::memcpy(&v, p + off, 4);
    return v;
}

bool fin(float v) { return std::isfinite(v); }

Status check(const WaveObject& w) {
    const float fs[] = {w.startDistance, w.endDistance, w.cycleTime, w.fadeInStart, w.fadeInEnd, w.fadeOutStart, w.fadeOutEnd,
                        w.restartDelayMin, w.restartDelayMax, w.tint[0], w.tint[1], w.tint[2], w.hexDiameter};
    for (float f : fs)
        if (!fin(f)) return Status::fail(Err::BadLayout, "waves: a setting is not finite");
    if (w.pagesU == 0 || w.pagesV == 0 || w.pagesInUse == 0)
        return Status::fail(Err::BadLayout, "waves: an atlas page count is 0");
    if (static_cast<uint64_t>(w.pagesInUse) > static_cast<uint64_t>(w.pagesU) * w.pagesV)
        return Status::fail(Err::BadLayout, "waves: more pages in use than the atlas holds");
    if (!(w.cycleTime > 0)) return Status::fail(Err::BadLayout, "waves: cycle time <= 0");
    if (!(w.fadeInEnd > w.fadeInStart) || !(w.fadeOutEnd > w.fadeOutStart))
        return Status::fail(Err::BadLayout, "waves: a fade ends before it starts");
    if (w.restartDelayMax < w.restartDelayMin) return Status::fail(Err::BadLayout, "waves: restart delay max < min");
    if (!(w.hexDiameter > 0)) return Status::fail(Err::BadLayout, "waves: hex diameter <= 0");
    return Status::Ok();
}

// value of attribute `name` in the first element of xml; false if missing
bool attr(const std::string& xml, const char* name, double& out) {
    size_t at = 0;
    const std::string key(name);
    for (;;) {
        at = xml.find(key, at);
        if (at == std::string::npos) return false;
        const bool startOk = at == 0 || std::isspace(static_cast<unsigned char>(xml[at - 1]));
        size_t q = at + key.size();
        while (q < xml.size() && std::isspace(static_cast<unsigned char>(xml[q]))) ++q;
        if (startOk && q < xml.size() && xml[q] == '=') {
            ++q;
            while (q < xml.size() && std::isspace(static_cast<unsigned char>(xml[q]))) ++q;
            if (q >= xml.size() || xml[q] != '"') return false;
            const size_t e = xml.find('"', q + 1);
            if (e == std::string::npos) return false;
            const std::string v = xml.substr(q + 1, e - q - 1);
            char* end = nullptr;
            out = std::strtod(v.c_str(), &end);
            return end && *end == '\0' && !v.empty();
        }
        at += key.size();
    }
}

// The LCG of the update (0x6AB850): 32-bit multiply-add, then the unsigned remainder by 0xFFFFCFEF.
uint32_t lcg(uint32_t x) { return (x * 0x343FDu + 0x269EC3u) % 0xFFFFCFEFu; }

const uint32_t kOwnBytes = kWavesPerChunk * 16;   // 0xF80
static_assert(sizeof(WaveRecord) == 16, "a wave record is 16 bytes, as the EXE's");

} // namespace

Status parseWaveObject(const uint8_t* p, size_t n, WaveObject& out) {
    out = WaveObject();
    if (!p || n < kWaveObjectBytes)
        return Status::fail(Err::Truncated, "waves: object of " + std::to_string(n) + " bytes, 0xB0 needed");
    WaveObject w;
    w.seed = rdU(p, 0x04);
    w.pagesU = rdU(p, 0x08);
    w.pagesV = rdU(p, 0x0C);
    w.pagesInUse = rdU(p, 0x10);
    w.startDistance = rdF(p, 0x1C);
    w.endDistance = rdF(p, 0x20);
    w.cycleTime = rdF(p, 0x24);
    w.fadeInStart = rdF(p, 0x28);
    w.fadeInEnd = rdF(p, 0x2C);
    w.fadeOutStart = rdF(p, 0x30);
    w.fadeOutEnd = rdF(p, 0x34);
    w.restartDelayMin = rdF(p, 0x38);
    w.restartDelayMax = rdF(p, 0x3C);
    w.tint[0] = rdF(p, 0x40);
    w.tint[1] = rdF(p, 0x44);
    w.tint[2] = rdF(p, 0x48);
    w.hexDiameter = rdF(p, 0x50);
    w.chunkCount = rdU(p, 0x58);
    w.waveCount = rdU(p, 0x60);
    C5R_TRY(check(w));
    out = w;
    return Status::Ok();
}

Status waveObjectFromSettings(const std::string& xmlIn, float hexDiameter, WaveObject& out) {
    out = WaveObject();
    std::string xml;   // the text without its comments (the shipped file explains the attributes in one)
    for (size_t at = 0; at < xmlIn.size();) {
        const size_t c = xmlIn.find("<!--", at);
        if (c == std::string::npos) {
            xml.append(xmlIn, at, std::string::npos);
            break;
        }
        xml.append(xmlIn, at, c - at);
        const size_t e = xmlIn.find("-->", c + 4);
        if (e == std::string::npos) return Status::fail(Err::BadLayout, "waves: WaveSettings has an unclosed comment");
        at = e + 3;
    }
    const char* names[] = {"CrestPagesU", "CrestPagesV", "CrestPagesInUse", "StartDistance", "EndDistance", "CycleTime",
                           "FadeInStart", "FadeInEnd", "FadeOutStart", "FadeOutEnd", "RestartDelayMin", "RestartDelayMax",
                           "Color_R", "Color_G", "Color_B"};
    double v[15];
    for (int i = 0; i < 15; ++i)
        if (!attr(xml, names[i], v[i])) return Status::fail(Err::BadLayout, std::string("waves: WaveSettings has no ") + names[i]);
    for (int i = 0; i < 3; ++i)
        if (v[i] < 0 || v[i] > 4096 || v[i] != std::floor(v[i]))
            return Status::fail(Err::BadLayout, std::string("waves: ") + names[i] + " is not a page count");
    WaveObject w;
    w.pagesU = static_cast<uint32_t>(v[0]);
    w.pagesV = static_cast<uint32_t>(v[1]);
    w.pagesInUse = static_cast<uint32_t>(v[2]);
    w.startDistance = static_cast<float>(v[3]);
    w.endDistance = static_cast<float>(v[4]);
    w.cycleTime = static_cast<float>(v[5]);
    w.fadeInStart = static_cast<float>(v[6]);
    w.fadeInEnd = static_cast<float>(v[7]);
    w.fadeOutStart = static_cast<float>(v[8]);
    w.fadeOutEnd = static_cast<float>(v[9]);
    w.restartDelayMin = static_cast<float>(v[10]);
    w.restartDelayMax = static_cast<float>(v[11]);
    // the EXE rounds Color / 255 and the exponent to float before pow (the capture's tint matches only that way)
    for (int c = 0; c < 3; ++c)
        w.tint[c] = static_cast<float>(std::pow(static_cast<double>(static_cast<float>(v[12 + c] / 255.0)), static_cast<double>(2.2f)));
    w.hexDiameter = hexDiameter;
    C5R_TRY(check(w));
    out = w;
    return Status::Ok();
}

Status computeWaveFrame(const float viewProj[16], const WaveObject& w, float planeZ, uint8_t out[kWaveFrameBytes]) {
    if (!out || !viewProj) return Status::fail(Err::BadLayout, "waves: out or viewProj null");
    for (int i = 0; i < 16; ++i)
        if (!fin(viewProj[i])) return Status::fail(Err::BadLayout, "waves: viewProj not finite");
    if (!fin(planeZ)) return Status::fail(Err::BadLayout, "waves: plane z not finite");
    C5R_TRY(check(w));
    float f[kWaveFrameBytes / 4] = {0};
    std::memcpy(f, viewProj, 64);
    f[0x10] = w.hexDiameter * w.startDistance;
    f[0x11] = w.hexDiameter * w.endDistance;
    f[0x12] = 1.0f / w.cycleTime;
    f[0x13] = planeZ;
    f[0x14] = w.fadeInStart;
    f[0x15] = 1.0f / (w.fadeInEnd - w.fadeInStart);
    f[0x16] = w.fadeOutStart;
    f[0x17] = 1.0f / (w.fadeOutEnd - w.fadeOutStart);
    f[0x18] = 1.0f / static_cast<float>(w.pagesU);
    f[0x19] = 1.0f / static_cast<float>(w.pagesV);
    f[0x1c] = w.tint[0];
    f[0x1d] = w.tint[1];
    f[0x1e] = w.tint[2];
    f[0x1f] = 1.0f;
    std::memcpy(out, f, kWaveFrameBytes);
    return Status::Ok();
}

void waveAtlasCell(const WaveObject& w, uint32_t index, float* u, float* v) {
    const uint32_t U = w.pagesU ? w.pagesU : 1, V = w.pagesV ? w.pagesV : 1;
    if (u) *u = static_cast<float>(static_cast<double>(index % U) / static_cast<double>(U));
    if (v) *v = static_cast<float>(static_cast<double>(index / U) / static_cast<double>(V));
}

Status updateWaves(const WaveObject& w, float dt, std::vector<WaveRecord>& waves) {
    if (!fin(dt) || dt < 0) return Status::fail(Err::BadLayout, "waves: dt not finite or negative");
    C5R_TRY(check(w));
    uint32_t state = w.seed;   // the EXE starts from Wave+0x04 at every call and does not store the state back
    for (WaveRecord& r : waves) {
        r.age += dt;
        if (r.age > w.cycleTime) {
            const uint32_t a = lcg(state);
            const double rand01 = static_cast<double>(a) * (1.0 / 4294954991.0);   // [0x9E7C38] = 1 / 0xFFFFCFEF
            r.age = static_cast<float>(-static_cast<double>(w.restartDelayMin) -
                                       rand01 * (static_cast<double>(w.restartDelayMax) - static_cast<double>(w.restartDelayMin)));
            state = lcg(a);
            waveAtlasCell(w, state % w.pagesInUse, &r.u, &r.v);
        }
    }
    return Status::Ok();
}

struct WaveRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbOwn = nullptr;
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11Resource* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    struct Chunk {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        uint32_t indices = 0, firstWave = 0, waveCount = 0;
    };
    std::vector<Chunk> chunks;
    void clearChunks() {
        for (Chunk& c : chunks) {
            rel(c.vb);
            rel(c.ib);
        }
        chunks.clear();
    }
    ~Impl() {
        clearChunks();
        rel(srv); rel(tex); rel(raster); rel(depth); rel(blend); rel(sampler); rel(cbFrame); rel(cbOwn); rel(layout); rel(ps);
        rel(vs); rel(dev);
    }
};

WaveRenderer::WaveRenderer() {}
WaveRenderer::~WaveRenderer() { release(); }

void WaveRenderer::release() {
    delete p_;
    p_ = nullptr;
}

Status WaveRenderer::init(ID3D11Device* dev, const shaders::ShaderSection& sec, const gamefs::GameFiles& files) {
    release();
    if (!dev) return Status::fail(Err::BadLayout, "waves: no device");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("WaveShader");
    if (!set || set->passes.empty()) return Status::fail(Err::NotFound, "waves: terrainsets has no WaveShader");
    C5R_TRY(terrainsets::validateSet(*set, sec));
    const terrainsets::PassDesc& pd = set->passes[0];
    const shaders::ShaderBlob* bv = sec.findByVa(pd.sm50[0]);
    const shaders::ShaderBlob* bp = sec.findByVa(pd.sm50[1]);
    if (!bv || !bp) return Status::fail(Err::NotFound, "waves: a WaveShader blob is not in the .shader section");
    if (bv->stage != shaders::Stage::Vertex || bp->stage != shaders::Stage::Pixel)
        return Status::fail(Err::BadLayout, "waves: a WaveShader blob has another stage");
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    C5R_TRY(hr(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (EXE)"));
    C5R_TRY(hr(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (EXE)"));
    // vertex format id 0x19 as the capture shows it [L 3601]
    const D3D11_INPUT_ELEMENT_DESC il[3] = {
        {"POSITION", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    C5R_TRY(hr(dev->CreateInputLayout(il, 3, bv->bytes, bv->size, &p->layout), "CreateInputLayout"));
    D3D11_BUFFER_DESC cd = {};
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.ByteWidth = kOwnBytes;
    C5R_TRY(hr(dev->CreateBuffer(&cd, nullptr, &p->cbOwn), "CreateBuffer (wave records)"));
    cd.ByteWidth = kWaveFrameBytes;
    C5R_TRY(hr(dev->CreateBuffer(&cd, nullptr, &p->cbFrame), "CreateBuffer (WaveFrame)"));
    const terrainsets::SamplerStateDesc* s3 = terrainsets::samplerState(3);
    if (!s3) return Status::fail(Err::NotFound, "waves: sampler state 3 not described");
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = static_cast<D3D11_FILTER>(s3->filter);
    sm.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s3->address[0]);
    sm.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s3->address[1]);
    sm.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(s3->address[2]);
    sm.MaxAnisotropy = s3->maxAnisotropy;
    sm.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(s3->comparison);
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    C5R_TRY(hr(dev->CreateSamplerState(&sm, &p->sampler), "CreateSamplerState"));
    // states of the EXE's wave draw [L 3601's capture]
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hr(dev->CreateBlendState(&bd, &p->blend), "CreateBlendState"));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    C5R_TRY(hr(dev->CreateDepthStencilState(&dd, &p->depth), "CreateDepthStencilState"));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    C5R_TRY(hr(dev->CreateRasterizerState(&rd, &p->raster), "CreateRasterizerState"));
    std::vector<uint8_t> bytes;
    Status st = files.read(kWaveTextureName, bytes);
    if (!st.ok()) return Status::fail(st.code, std::string("waves: ") + kWaveTextureName + ": " + st.msg);
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
    C5R_TRY(gamefs::createTexture(dev, img, &p->tex, &p->srv));
    p_ = p.release();
    return Status::Ok();
}

Status WaveRenderer::setChunks(const std::vector<WaveChunk>& chunks) {
    if (!p_) return Status::fail(Err::NotFound, "waves: init not done");
    std::vector<Impl::Chunk> made;
    auto drop = [&made] {
        for (Impl::Chunk& c : made) {
            rel(c.vb);
            rel(c.ib);
        }
    };
    for (size_t i = 0; i < chunks.size(); ++i) {
        const WaveChunk& c = chunks[i];
        const std::string where = "waves: chunk " + std::to_string(i);
        if (c.vb.empty() || c.vb.size() % kWaveVertexBytes) {
            drop();
            return Status::fail(Err::BadLayout, where + ": vertex buffer of " + std::to_string(c.vb.size()) + " bytes");
        }
        if (c.ib.empty() || c.ib.size() % 6) {
            drop();
            return Status::fail(Err::BadLayout, where + ": index buffer of " + std::to_string(c.ib.size()) + " bytes (whole triangles of R16)");
        }
        if (c.waveCount == 0 || c.waveCount > kWavesPerChunk) {
            drop();
            return Status::fail(Err::BadLayout, where + ": " + std::to_string(c.waveCount) + " waves");
        }
        const size_t verts = c.vb.size() / kWaveVertexBytes;
        for (size_t k = 0; k + 1 < c.ib.size(); k += 2) {
            uint16_t ix;
            std::memcpy(&ix, &c.ib[k], 2);
            if (ix >= verts) {
                drop();
                return Status::fail(Err::BadLayout, where + ": index " + std::to_string(ix) + " past " + std::to_string(verts) + " vertices");
            }
        }
        Impl::Chunk m;
        m.indices = static_cast<uint32_t>(c.ib.size() / 2);
        m.firstWave = c.firstWave;
        m.waveCount = c.waveCount;
        D3D11_BUFFER_DESC d = {};
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.ByteWidth = static_cast<UINT>(c.vb.size());
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s = {c.vb.data(), 0, 0};
        Status st = hr(p_->dev->CreateBuffer(&d, &s, &m.vb), "CreateBuffer (wave VB)");
        if (st.ok()) {
            d.ByteWidth = static_cast<UINT>(c.ib.size());
            d.BindFlags = D3D11_BIND_INDEX_BUFFER;
            s.pSysMem = c.ib.data();
            st = hr(p_->dev->CreateBuffer(&d, &s, &m.ib), "CreateBuffer (wave IB)");
        }
        made.push_back(m);
        if (!st.ok()) {
            drop();
            return st;
        }
    }
    p_->clearChunks();
    p_->chunks = made;
    return Status::Ok();
}

Status WaveRenderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                          const float viewProj[16], const WaveObject& w, float planeZ, const std::vector<WaveRecord>& waves,
                          WaveStats* stats) {
    if (stats) *stats = WaveStats();
    if (!p_) return Status::fail(Err::NotFound, "waves: init not done");
    if (!ctx || !rtv) return Status::fail(Err::BadLayout, "waves: no context or render target");
    uint8_t frame[kWaveFrameBytes];
    C5R_TRY(computeWaveFrame(viewProj, w, planeZ, frame));
    for (const Impl::Chunk& c : p_->chunks)
        if (static_cast<uint64_t>(c.firstWave) + c.waveCount > waves.size())
            return Status::fail(Err::BadLayout, "waves: a chunk's waves lie past the wave array (" + std::to_string(waves.size()) + ")");
    ctx->UpdateSubresource(p_->cbFrame, 0, nullptr, frame, 0, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetBlendState(p_->blend, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(p_->depth, 0);
    ctx->RSSetState(p_->raster);
    ctx->IASetInputLayout(p_->layout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p_->vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(p_->ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = {p_->cbOwn, p_->cbFrame};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetShaderResources(0, 1, &p_->srv);
    ctx->PSSetSamplers(0, 1, &p_->sampler);
    std::vector<uint8_t> own(kOwnBytes, 0);
    for (const Impl::Chunk& c : p_->chunks) {
        std::memset(own.data(), 0, own.size());
        std::memcpy(own.data(), &waves[c.firstWave], static_cast<size_t>(c.waveCount) * sizeof(WaveRecord));
        ctx->UpdateSubresource(p_->cbOwn, 0, nullptr, own.data(), 0, 0);
        const UINT stride = kWaveVertexBytes, offset = 0;
        ctx->IASetVertexBuffers(0, 1, &c.vb, &stride, &offset);
        ctx->IASetIndexBuffer(c.ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->DrawIndexed(c.indices, 0, 0);
        if (stats) {
            ++stats->chunksDrawn;
            stats->indices += c.indices;
        }
    }
    return Status::Ok();
}

}} // namespace c5r::water
