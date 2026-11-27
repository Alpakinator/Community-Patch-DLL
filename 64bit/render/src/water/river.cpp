// river.cpp - c5r::water rivers: WaterShader pass 4 (or 1) over the tapped river mesh (epic 3600, issue 3605).
// Own code (team-3600 W4), written from research/05-graphics-composition/water-system.md sections 8.2, 8.3, 9.2 and 11
// (behaviour, D83). The EXE's shaders and the river art are read from the player's files at run time.
//
// What the EXE does (0x463640, the river render; 0x6B1580, the draw record): one draw per frame of the whole river
// mesh (triangle list, River+0x28 triangles), WaterShader pass 4 when [0xD90F68] is 1 or 2, else pass 1, with the own
// buffer's first 64 bytes = identity (the mesh is in world units), the shared WaterFrame as the ocean wrote it last,
// and the record's textures: t0-t3 the river LEAN pairs, t4 the reflection, t5 river_alpha_new, t6 river_diffuse_new
// (sRGB view), t7 River+0x24 (32 x 32, zero in stock [L]; read by neither river PS [C]), t8-t11 as the ocean, t12 the
// Rivers ColorKey of style Generic. Stock states [L, 3601's capture]: blend SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE),
// depth LESS_EQUAL with write, cull BACK with FrontCounterClockwise; samplers linear, clamp on t5, t6, t8, t11, t12.
#include "c5r/water/water.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include "c5r/bake/post.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/lean.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace water {

namespace {

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status hrOk(HRESULT h, const std::string& what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(h == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, what + b);
}

const uint32_t kRiverVs = 0x06816a20;
const uint32_t kRiverStride = 32;

DXGI_FORMAT typelessOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT srgbOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return f;
    }
}

Status ddsTexture(ID3D11Device* dev, const std::vector<uint8_t>& bytes, const char* name, bool srgb, ID3D11ShaderResourceView** out) {
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
    if (img.dim != gamefs::DdsDim::Tex2D || img.cube || img.arraySize != 1)
        return Status::fail(Err::Unsupported, std::string(name) + ": not a plain 2D texture");
    const DXGI_FORMAT tl = srgb ? typelessOf(img.format) : img.format;
    if (tl == DXGI_FORMAT_UNKNOWN) return Status::fail(Err::Unsupported, std::string(name) + ": no sRGB view for its format");
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = img.width;
    d.Height = img.height;
    d.MipLevels = img.mipLevels;
    d.ArraySize = 1;
    d.Format = tl;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const gamefs::DdsSubresource& s : img.subresources)
        init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + s.offset, s.rowPitch, s.slicePitch});
    ID3D11Texture2D* t = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture2D(&d, init.data(), &t), std::string("CreateTexture2D ") + name));
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = srgb ? srgbOf(img.format) : img.format;
    v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    v.Texture2D.MipLevels = img.mipLevels;
    Status st = hrOk(dev->CreateShaderResourceView(t, &v, out), std::string("CreateShaderResourceView ") + name);
    t->Release();
    return st;
}

Status tex2d(ID3D11Device* dev, DXGI_FORMAT f, uint32_t w, uint32_t h, const void* data, uint32_t pitch, ID3D11ShaderResourceView** out) {
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {data, pitch, 0};
    ID3D11Texture2D* t = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture2D(&d, &s, &t), "CreateTexture2D (river constant)"));
    Status st = hrOk(dev->CreateShaderResourceView(t, nullptr, out), "CreateShaderResourceView (river constant)");
    t->Release();
    return st;
}

Status volume(ID3D11Device* dev, const std::vector<uint8_t>& rgba, ID3D11ShaderResourceView** out) {
    if (rgba.size() != bake::kColorKeyTexels * 4) return Status::fail(Err::BadLayout, "ColorKey volume: not 32^3 RGBA8");
    D3D11_TEXTURE3D_DESC d = {};
    d.Width = d.Height = d.Depth = 32;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {rgba.data(), 32 * 4, 32 * 32 * 4};
    ID3D11Texture3D* t = nullptr;
    C5R_TRY(hrOk(dev->CreateTexture3D(&d, &s, &t), "CreateTexture3D (Rivers ColorKey)"));
    Status st = hrOk(dev->CreateShaderResourceView(t, nullptr, out), "CreateShaderResourceView (Rivers ColorKey)");
    t->Release();
    return st;
}

// The identity volume ColorKeySystem fills before it loads a file (x, y, z, 1) [C, asset-pipeline.md 3.5]: texel
// (x, y, z) = (x * 255 / 31, y * 255 / 31, z * 255 / 31, 255) [I: the exact scaling of the identity is not traced].
std::vector<uint8_t> identityVolume() {
    std::vector<uint8_t> v(bake::kColorKeyTexels * 4);
    for (uint32_t z = 0; z < 32; ++z)
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                uint8_t* p = v.data() + ((z * 32 + y) * 32 + x) * 4;
                p[0] = static_cast<uint8_t>(x * 255 / 31);
                p[1] = static_cast<uint8_t>(y * 255 / 31);
                p[2] = static_cast<uint8_t>(z * 255 / 31);
                p[3] = 255;
            }
    return v;
}

} // namespace

std::string riverColorKeyFile(uint32_t style) {
    const char* n = bake::styleName(style);
    return n ? std::string("Rivers_") + n + "_ColorKey.tga" : std::string();
}

Status loadRiverArt(const std::wstring& gameDir, RiverArt& out) {
    out = RiverArt();
    gamefs::GameFiles fs;
    C5R_TRY(fs.open(gameDir));
    terrainsets::LeanFile lf;
    C5R_TRY(terrainsets::loadLean(fs, terrainsets::kLeanRiver1, out.leanBytes[0], lf));
    C5R_TRY(terrainsets::loadLean(fs, terrainsets::kLeanRiver2, out.leanBytes[1], lf));
    Status st = fs.read("river_alpha_new.dds", out.alphaDds);
    if (!st.ok()) return Status::fail(Err::NotFound, "river_alpha_new.dds: " + st.str());
    st = fs.read("river_diffuse_new.dds", out.diffuseDds);
    if (!st.ok()) return Status::fail(Err::NotFound, "river_diffuse_new.dds: " + st.str());
    for (uint32_t s = 0; s < 5; ++s) {
        std::vector<uint8_t> tga;
        if (fs.read(riverColorKeyFile(s), tga).ok()) {
            C5R_TRY(bake::buildColorKeyLut(tga.data(), tga.size(), out.colorKey[s]));
            out.colorKeyFromFile[s] = true;
        } else {
            out.colorKey[s] = identityVolume();  // no file: the identity, as the EXE leaves it
        }
    }
    return Status::Ok();
}

struct RiverRenderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps[5] = {};   // passes 1 and 4
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbOwn = nullptr;
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_R32_UINT;
    std::vector<Mesh::Draw> draws;
    std::vector<uint8_t> own;         // 0x80: identity, or the tapped own constants
    ID3D11ShaderResourceView* mine[14] = {};    // t0-t3, t5, t6, t7
    ID3D11ShaderResourceView* given[14] = {};
    ID3D11ShaderResourceView* colorKey[5] = {};
    ID3D11ShaderResourceView* givenColorKey = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11BlendState* bs = nullptr;

    void dropMesh() {
        rel(vb);
        rel(ib);
        draws.clear();
    }
    ~Impl() {
        dropMesh();
        rel(vs);
        for (auto& x : ps) rel(x);
        rel(layout);
        rel(cbOwn);
        rel(cbFrame);
        for (auto& x : mine) rel(x);
        for (auto& x : given) rel(x);
        for (auto& x : colorKey) rel(x);
        rel(givenColorKey);
        rel(rs);
        rel(dss);
        rel(bs);
        rel(dev);
    }
};

RiverRenderer::RiverRenderer() {}
RiverRenderer::~RiverRenderer() { release(); }
void RiverRenderer::release() {
    delete p_;
    p_ = nullptr;
}
bool RiverRenderer::hasMesh() const { return p_ && p_->vb && !p_->draws.empty(); }

Status RiverRenderer::init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir) {
    release();
    if (!dev) return Status::fail(Err::BadLayout, "river init: no device");
    const terrainsets::ShaderSetDesc* set = terrainsets::findSet("WaterShader");
    if (!set) return Status::fail(Err::NotFound, "terrainsets has no WaterShader");
    shaders::ShaderSection sec;
    C5R_TRY(sec.open(exePath));
    std::unique_ptr<Impl> p(new Impl());
    p->dev = dev;
    dev->AddRef();
    for (const terrainsets::PassDesc& pd : set->passes) {
        if (pd.index != 1 && pd.index != 4) continue;
        const shaders::ShaderBlob* bv = sec.findByVa(pd.sm50[0]);
        const shaders::ShaderBlob* bp = sec.findByVa(pd.sm50[1]);
        if (!bv || !bp || bv->stage != shaders::Stage::Vertex || bp->stage != shaders::Stage::Pixel || pd.sm50[0] != kRiverVs)
            return Status::fail(Err::NotFound, "WaterShader river pass " + std::to_string(pd.index) + ": SM5 VS 0x06816a20 / PS not in the .shader section");
        if (!p->vs) {
            C5R_TRY(hrOk(dev->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (river)"));
            // the stock input layout [L]: POSITION R32G32B32_FLOAT, TEXCOORD0 R32G32 @12, TEXCOORD1 R32G32 @20, COLOR
            // R8G8B8A8_UNORM @28 (not read by the VS)
            const D3D11_INPUT_ELEMENT_DESC e[4] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            C5R_TRY(hrOk(dev->CreateInputLayout(e, 4, bv->bytes, bv->size, &p->layout), "CreateInputLayout (river)"));
        }
        C5R_TRY(hrOk(dev->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps[pd.index]), "CreatePixelShader (river)"));
    }
    if (!p->vs || !p->ps[1] || !p->ps[4]) return Status::fail(Err::NotFound, "WaterShader river passes 1 and 4 missing");
    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.ByteWidth = 0x80;
    C5R_TRY(hrOk(dev->CreateBuffer(&cbd, nullptr, &p->cbOwn), "CreateBuffer (river own)"));
    cbd.ByteWidth = kWaterFrameBytes;
    C5R_TRY(hrOk(dev->CreateBuffer(&cbd, nullptr, &p->cbFrame), "CreateBuffer (river WaterFrame)"));
    p->own.assign(0x80, 0);
    const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(p->own.data(), ident, sizeof ident);
    // art
    const uint32_t zero = 0;
    std::vector<uint8_t> zeros32(32 * 32 * 4, 0);
    C5R_TRY(tex2d(dev, DXGI_FORMAT_R8G8B8A8_UNORM, 32, 32, zeros32.data(), 32 * 4, &p->mine[7]));  // River+0x24: zero [L]
    (void)zero;
    if (!gameDir.empty()) {
        RiverArt art;
        C5R_TRY(loadRiverArt(gameDir, art));
        for (int k = 0; k < 2; ++k) {
            terrainsets::LeanFile lf;
            C5R_TRY(terrainsets::parseLean(art.leanBytes[k].data(), art.leanBytes[k].size(), lf));
            ID3D11Texture2D* four = nullptr;
            ID3D11Texture2D* one = nullptr;
            C5R_TRY(terrainsets::createLeanTextures(dev, lf, &four, &one));
            Status s1 = hrOk(dev->CreateShaderResourceView(four, nullptr, &p->mine[k * 2]), "CreateShaderResourceView (lean)");
            Status s2 = hrOk(dev->CreateShaderResourceView(one, nullptr, &p->mine[k * 2 + 1]), "CreateShaderResourceView (lean)");
            four->Release();
            one->Release();
            C5R_TRY(s1);
            C5R_TRY(s2);
        }
        C5R_TRY(ddsTexture(dev, art.alphaDds, "river_alpha_new.dds", false, &p->mine[5]));
        C5R_TRY(ddsTexture(dev, art.diffuseDds, "river_diffuse_new.dds", true, &p->mine[6]));
        for (uint32_t s = 0; s < 5; ++s) C5R_TRY(volume(dev, art.colorKey[s], &p->colorKey[s]));
    } else {
        const uint16_t lean[4] = {0, 0, 0x8000, 0x8000};
        const uint16_t lean1 = 0x8000;
        const uint32_t white = 0xFFFFFFFFu, blue = 0xFF8A6A3Au;
        for (int k = 0; k < 2; ++k) {
            C5R_TRY(tex2d(dev, DXGI_FORMAT_R16G16B16A16_UNORM, 1, 1, lean, 8, &p->mine[k * 2]));
            C5R_TRY(tex2d(dev, DXGI_FORMAT_R16_UNORM, 1, 1, &lean1, 2, &p->mine[k * 2 + 1]));
        }
        C5R_TRY(tex2d(dev, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, &white, 4, &p->mine[5]));
        C5R_TRY(tex2d(dev, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, &blue, 4, &p->mine[6]));
        const std::vector<uint8_t> idv = identityVolume();
        for (uint32_t s = 0; s < 5; ++s) C5R_TRY(volume(dev, idv, &p->colorKey[s]));
    }
    // states of the stock river draw [L]
    D3D11_RASTERIZER_DESC r = {};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_BACK;
    r.FrontCounterClockwise = TRUE;
    r.DepthClipEnable = TRUE;
    C5R_TRY(hrOk(dev->CreateRasterizerState(&r, &p->rs), "CreateRasterizerState (river)"));
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    C5R_TRY(hrOk(dev->CreateDepthStencilState(&ds, &p->dss), "CreateDepthStencilState (river)"));
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    C5R_TRY(hrOk(dev->CreateBlendState(&bd, &p->bs), "CreateBlendState (river)"));
    p_ = p.release();
    return Status::Ok();
}

Status RiverRenderer::setMesh(const Mesh& m) {
    if (!p_) return Status::fail(Err::NotFound, "river setMesh: init not done");
    if (m.vb.empty() && m.ib.empty() && m.draws.empty()) {
        p_->dropMesh();
        return Status::Ok();
    }
    if (m.stride != kRiverStride) return Status::fail(Err::BadLayout, "river setMesh: stride " + std::to_string(m.stride) + ", not 32");
    if (m.vb.empty() || m.vb.size() % kRiverStride) return Status::fail(Err::BadLayout, "river setMesh: vertex bytes not a multiple of 32");
    uint32_t isz = 0;
    if (m.indexFormat == DXGI_FORMAT_R32_UINT) isz = 4;
    else if (m.indexFormat == DXGI_FORMAT_R16_UINT) isz = 2;
    else return Status::fail(Err::BadLayout, "river setMesh: index format neither R32 nor R16");
    if (m.ib.empty() || m.ib.size() % isz) return Status::fail(Err::BadLayout, "river setMesh: index bytes not whole indices");
    if (m.draws.empty()) return Status::fail(Err::BadLayout, "river setMesh: no draw");
    if (!m.ownConstants.empty() && m.ownConstants.size() < 64)
        return Status::fail(Err::BadLayout, "river setMesh: own constants shorter than the 64-byte World");
    const uint64_t vertices = m.vb.size() / kRiverStride, indices = m.ib.size() / isz;
    for (const Mesh::Draw& d : m.draws) {
        if (d.topology != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) return Status::fail(Err::BadLayout, "river setMesh: a draw not a triangle list");
        if (d.pass != 0 && d.pass != 1 && d.pass != 4) return Status::fail(Err::BadLayout, "river setMesh: pass not 0, 1 or 4");
        if (d.indexCount == 0 || d.indexCount % 3 || static_cast<uint64_t>(d.startIndex) + d.indexCount > indices)
            return Status::fail(Err::BadLayout, "river setMesh: a draw's indices lie outside the index buffer or are not whole triangles");
        if (d.baseVertex < 0 || static_cast<uint64_t>(d.baseVertex) >= vertices)
            return Status::fail(Err::BadLayout, "river setMesh: base vertex outside the vertex buffer");
    }
    // every index inside the vertex buffer (the tap checked them on the EXE side; checked again here)
    for (uint64_t i = 0; i < indices; ++i) {
        uint32_t ix = 0;
        std::memcpy(&ix, m.ib.data() + i * isz, isz);
        if (ix >= vertices) return Status::fail(Err::BadLayout, "river setMesh: index " + std::to_string(i) + " past the vertex buffer");
    }
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    D3D11_BUFFER_DESC d = {};
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.ByteWidth = static_cast<UINT>(m.vb.size());
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA s = {m.vb.data(), 0, 0};
    C5R_TRY(hrOk(p_->dev->CreateBuffer(&d, &s, &vb), "CreateBuffer (river VB)"));
    d.ByteWidth = static_cast<UINT>(m.ib.size());
    d.BindFlags = D3D11_BIND_INDEX_BUFFER;
    s.pSysMem = m.ib.data();
    Status st = hrOk(p_->dev->CreateBuffer(&d, &s, &ib), "CreateBuffer (river IB)");
    if (!st.ok()) {
        vb->Release();
        return st;
    }
    p_->dropMesh();
    p_->vb = vb;
    p_->ib = ib;
    p_->ibFormat = static_cast<DXGI_FORMAT>(m.indexFormat);
    p_->draws = m.draws;
    if (!m.ownConstants.empty()) {
        p_->own.assign(0x80, 0);
        std::memcpy(p_->own.data(), m.ownConstants.data(), std::min<size_t>(0x80, m.ownConstants.size()));
    }
    return Status::Ok();
}

Status RiverRenderer::setTexture(RiverSlot slot, ID3D11ShaderResourceView* srv) {
    if (!p_) return Status::fail(Err::NotFound, "river setTexture: init not done");
    const uint32_t i = static_cast<uint32_t>(slot);
    if (i == 12) {
        if (srv) srv->AddRef();
        rel(p_->givenColorKey);
        p_->givenColorKey = srv;
        return Status::Ok();
    }
    if (!(i <= 3 || i == 5 || i == 6 || i == 7)) return Status::fail(Err::BadLayout, "river setTexture: slot " + std::to_string(i) + " is not a river input");
    if (srv) srv->AddRef();
    rel(p_->given[i]);
    p_->given[i] = srv;
    return Status::Ok();
}

Status RiverRenderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t width,
                           uint32_t height, const uint8_t frame[kWaterFrameBytes], const RiverShared& shared, int pass,
                           uint32_t style, uint32_t* draws, int* passUsed) {
    if (draws) *draws = 0;
    if (!p_) return Status::fail(Err::NotFound, "river draw: init not done");
    if (!hasMesh()) return Status::fail(Err::NotFound, "river draw: no mesh");
    if (!ctx || !rtv || !frame || width == 0 || height == 0)
        return Status::fail(Err::BadLayout, "river draw: context, target, frame or size missing");
    if (pass != -1 && pass != 1 && pass != 4) return Status::fail(Err::BadLayout, "river draw: pass " + std::to_string(pass) + " is not -1, 1 or 4");
    if (style >= 5) return Status::fail(Err::BadLayout, "river draw: style " + std::to_string(style) + " outside 0..4");
    Impl& p = *p_;
    D3D11_MAPPED_SUBRESOURCE m;
    C5R_TRY(hrOk(ctx->Map(p.cbOwn, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (river own)"));
    std::memcpy(m.pData, p.own.data(), 0x80);
    ctx->Unmap(p.cbOwn, 0);
    C5R_TRY(hrOk(ctx->Map(p.cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (river WaterFrame)"));
    std::memcpy(m.pData, frame, kWaterFrameBytes);
    ctx->Unmap(p.cbFrame, 0);
    const D3D11_VIEWPORT vpt = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    ctx->RSSetViewports(1, &vpt);
    ctx->RSSetState(p.rs);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetDepthStencilState(p.dss, 0);
    ctx->OMSetBlendState(p.bs, nullptr, 0xFFFFFFFFu);
    ctx->IASetInputLayout(p.layout);
    const UINT stride = kRiverStride, off = 0;
    ctx->IASetVertexBuffers(0, 1, &p.vb, &stride, &off);
    ctx->IASetIndexBuffer(p.ib, p.ibFormat, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* cbs[2] = {p.cbOwn, p.cbFrame};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    // samplers as the stock river draw [L]: linear everywhere; t5, t6 (its sRGB view), t8, t11, t12 clamp
    ID3D11SamplerState* ss[14];
    for (uint32_t i = 0; i < 14; ++i) ss[i] = shared.samplers[0];
    ss[5] = shared.samplers[5];
    ss[6] = shared.samplers[3];
    ss[8] = shared.samplers[5];
    ss[11] = shared.samplers[5];
    ss[12] = shared.samplers[5];
    for (ID3D11SamplerState* s : ss)
        if (!s) return Status::fail(Err::BadLayout, "river draw: a shared sampler state is null");
    ctx->PSSetSamplers(0, 14, ss);
    ID3D11ShaderResourceView* srv[14] = {};
    for (uint32_t i : {0u, 1u, 2u, 3u, 5u, 6u, 7u}) srv[i] = p.given[i] ? p.given[i] : p.mine[i];
    for (uint32_t i : {4u, 8u, 9u, 10u, 11u, 13u}) srv[i] = shared.srv[i];
    srv[12] = p.givenColorKey ? p.givenColorKey : p.colorKey[style];
    ctx->PSSetShaderResources(0, 14, srv);
    uint32_t n = 0;
    int used = -1;
    for (const Mesh::Draw& d : p.draws) {
        const int dp = pass >= 0 ? pass : (d.pass == 1 ? 1 : 4);
        ctx->PSSetShader(p.ps[dp], nullptr, 0);
        ctx->DrawIndexed(d.indexCount, d.startIndex, d.baseVertex);
        used = dp;
        ++n;
    }
    ID3D11ShaderResourceView* none[14] = {};
    ctx->PSSetShaderResources(0, 14, none);
    if (draws) *draws = n;
    if (passUsed) *passUsed = used;
    return Status::Ok();
}

}} // namespace c5r::water
