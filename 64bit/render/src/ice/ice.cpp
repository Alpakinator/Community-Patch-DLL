// ice.cpp - c5r::ice: the polar ice drawn with the EXE's IceShader programs (epic 0169, seam G, issue 4306).
// Own code, written from the behaviour described in research/05-graphics-composition/polar-ice.md sections 2, 3 and 7
// (the ice draw 0x5FACB0, its IceFrame stores, the mesh and the Ice object; D83: no decompiler or listing text here).
// The EXE's shaders and the ice's texture files are read from the player's files at run time; nothing of them is
// copied here.
//
// The arithmetic of computeIceFrame follows the draw's: single precision where it uses SSE, double precision where it
// uses the x87 unit (row 29.y), rounded to float where it stores.
#include "c5r/ice/ice.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/lean.h"
#include "c5r/water/water.h"

namespace c5r { namespace ice {

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

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

bool finite16(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// out = a * b (row-major), each element summed left to right in float (D3DXMatrixMultiply's scalar form, as
// c5r::water's WaterFrame).
void mul44f(const float* a, const float* b, float* out) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = a[i * 4 + 0] * b[0 * 4 + j];
            s += a[i * 4 + 1] * b[1 * 4 + j];
            s += a[i * 4 + 2] * b[2 * 4 + j];
            s += a[i * 4 + 3] * b[3 * 4 + j];
            r[i * 4 + j] = s;
        }
    std::memcpy(out, r, sizeof r);
}

bool inverse44(const float* m, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c)
            for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[piv][j]);
        const double d = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<float>(a[i][j + 4]);
    return true;
}

void put(uint8_t* out, uint32_t off, const float* v, uint32_t n) { std::memcpy(out + off, v, n * 4); }
void put1(uint8_t* out, uint32_t off, float v) { std::memcpy(out + off, &v, 4); }

// The draw's stores into IceFrame (byte offset, float count); polar-ice.md 7.
struct Run {
    uint32_t off, count;
};
const Run kStored[] = {
    {0x000, 16},  // rows 0-3 (T_k * V) * P
    {0x040, 2},   // row 4.xy  box min
    {0x050, 2},   // row 5.xy  box max
    {0x060, 1},   // row 6.x   +0xFFC0 + +0xFFC4
    {0x070, 1},   // row 7.x   +0xFF14 - +0xFFC4
    {0x080, 16},  // rows 8-11 T_k * OverlayProj
    {0x0C0, 3},   // row 12.xyz camera position of the copy
    {0x0D0, 4},   // row 13    FOWAnim
    {0x0E0, 3},   // row 14.xyz +0xFF38
    {0x0F0, 3},   // row 15.xyz +0xFF44
    {0x100, 1},   // row 16.x  +0xFF0C
    {0x110, 1},   // row 17.x  +0xFFD8
    {0x120, 1},   // row 18.x  +0xFFDC
    {0x130, 1},   // row 19.x  +0xFFE0
    {0x140, 3},   // row 20.xyz +0xFFF4
    {0x150, 1},   // row 21.x  +0xFF14
    {0x160, 2},   // row 22.xy +0xFFE4
    {0x170, 2},   // row 23.xy +0xFFEC
    {0x180, 3},   // row 24.xyz +0x10000
    {0x190, 3},   // row 25.xyz +0x1000C
    {0x1A0, 1},   // row 26.x  +0x10018
    {0x1B0, 1},   // row 27.x  +0x1001C
    {0x1C0, 4},   // row 28    DistanceFog colour
    {0x1D0, 4},   // row 29    (-density, fog term, 0, zoom fraction)
};

// The field offsets the draw reads (each a float; checked finite by setMesh and computeIceFrame).
const uint32_t kReadFields[] = {
    field::kLeanScale, field::kWaterHeight, field::kBox, field::kBox + 4, field::kBox + 8, field::kBox + 12,
    field::kDirA, field::kDirA + 4, field::kDirA + 8, field::kDirB, field::kDirB + 4, field::kDirB + 8,
    field::kHeightAbove, field::kDepthBelow, field::kTextureRepeat, field::kBumpRepeat, field::kSpecularPower,
    field::kSideTexRepeat, field::kSideTexRepeat + 4, field::kSideBumpRepeat, field::kSideBumpRepeat + 4,
    field::kSubsurface, field::kSubsurface + 4, field::kSubsurface + 8, field::kUnderwater1, field::kUnderwater1 + 4,
    field::kUnderwater1 + 8, field::kUnderwater2, field::kUnderwater2 + 4, field::kUnderwater2 + 8, field::kColorRamp,
    field::kAlphaRamp,
};

Status checkFields(const IceFields& f, const char* who) {
    if (!f.valid) return Status::fail(Err::NotFound, std::string(who) + ": Ice fields not valid");
    for (uint32_t off : kReadFields)
        if (!std::isfinite(f.get(off)))
            return Status::fail(Err::BadLayout, std::string(who) + ": Ice field +" + hex(off) + " is not finite");
    return Status::Ok();
}

const D3D11_INPUT_ELEMENT_DESC kLayout[2] = {
    {"TEXCOORD", 0, DXGI_FORMAT_R16G16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

const char* const kTextureFiles[7] = {"Ice_Environment_Diffuse.dds", "Ice_D.dds", "Ice_Bump.lean", "Ice_Bump.lean",
                                      "Ice_D_Sides.dds", "Ice_Bump_Sides.lean", "Ice_Bump_Sides.lean"};

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

// The PS's resource declarations the module binds for (polar-ice.md 2, 7): slot -> D3D10_SB_RESOURCE_DIMENSION.
uint32_t expectedPsDim(uint32_t slot) {
    if (slot == 0) return 6;                   // texturecube
    if (slot == 10) return 5;                  // texture3d
    if (slot >= 1 && slot <= 9 && slot != 3) return 3;  // texture2d
    return 0;
}

Status checkVs(const shaders::ShaderReflection& r, uint32_t va) {
    const std::string who = "IceShader VS " + hex(va) + ": ";
    const auto& e = r.input.elems;
    if (e.size() != 2 || e[0].semantic != "TEXCOORD" || e[0].semanticIndex != 0 || e[0].reg != 0 ||
        e[0].componentType != 3 || (e[0].mask & 3) != 3 || e[1].semantic != "TEXCOORD" || e[1].semanticIndex != 1 ||
        e[1].reg != 1 || e[1].componentType != 3 || e[1].mask != 0xF)
        return Status::fail(Err::BadLayout, who + "input declaration is not TEXCOORD0.xy + TEXCOORD1.xyzw (float): " +
                                                shaders::sigSummary(r.input));
    for (const shaders::CbDecl& c : r.cbuffers) {
        if (c.slot != 1) return Status::fail(Err::BadLayout, who + "declares cb" + std::to_string(c.slot) + " (IceFrame is cb1 only)");
        if (c.vec4Count == 0 || c.vec4Count * 16 > kIceFrameBytes)
            return Status::fail(Err::BadLayout, who + "cb1 has " + std::to_string(c.vec4Count) + " rows; IceFrame has 30");
    }
    if (!r.findCb(1)) return Status::fail(Err::BadLayout, who + "does not declare cb1 (IceFrame)");
    return Status::Ok();
}

Status checkPs(const shaders::ShaderReflection& r, uint32_t va) {
    const std::string who = "IceShader PS " + hex(va) + ": ";
    if (r.cbuffers.size() != 1 || r.cbuffers[0].slot != 1)
        return Status::fail(Err::BadLayout, who + "constant buffers are not cb1 alone (IceFrame)");
    if (r.cbuffers[0].vec4Count * 16 != kIceFrameBytes)
        return Status::fail(Err::BadLayout, who + "cb1 has " + std::to_string(r.cbuffers[0].vec4Count) +
                                                " rows; the IceFrame layout is 30 (480 bytes)");
    uint32_t seen = 0;
    for (const shaders::ResourceDecl& d : r.resources) {
        if (d.uav || d.slot >= 16 || expectedPsDim(d.slot) == 0)
            return Status::fail(Err::BadLayout, who + "declares slot " + std::string(d.uav ? "u" : "t") + std::to_string(d.slot) +
                                                    ", which stock's ice draw does not bind");
        if (d.dimension != expectedPsDim(d.slot))
            return Status::fail(Err::BadLayout, who + "t" + std::to_string(d.slot) + " is a " +
                                                    shaders::resourceDimName(d.dimension) + ", not a " +
                                                    shaders::resourceDimName(expectedPsDim(d.slot)));
        seen |= 1u << d.slot;
    }
    const uint32_t want = 0x7F7u;  // t0-t2, t4-t10
    if (seen != want) return Status::fail(Err::BadLayout, who + "does not declare every slot of t0-t2, t4-t10");
    for (const shaders::SamplerDecl& s : r.samplers)
        if (s.slot > 10) return Status::fail(Err::BadLayout, who + "declares sampler s" + std::to_string(s.slot) + " (stock binds s0-s10)");
    return Status::Ok();
}

} // namespace

// ---- IceFields ----

float IceFields::get(uint32_t off) const {
    if (off < kFieldsBase || off + 4 > kFieldsEnd || (off & 3)) return 0.0f;
    float v;
    std::memcpy(&v, raw + (off - kFieldsBase), 4);
    return v;
}
void IceFields::set(uint32_t off, float v) {
    if (off < kFieldsBase || off + 4 > kFieldsEnd || (off & 3)) return;
    std::memcpy(raw + (off - kFieldsBase), &v, 4);
}

// ---- IceFrame (polar-ice.md 7) ----

bool iceFrameStored(uint32_t off) {
    for (const Run& r : kStored)
        if (off >= r.off && off < r.off + r.count * 4) return true;
    return false;
}

void copyCullBox(const FrameInputs& in, uint32_t copy, float out[4]) {
    std::memcpy(out, in.cullBox, 4 * sizeof(float));
    if (copy == 1) {  // the box moved by -W: (-W) + x, as the draw's SSE adds
        out[0] = -in.worldWidth + in.cullBox[0];
        out[2] = -in.worldWidth + in.cullBox[2];
    } else if (copy == 2) {
        out[0] = in.cullBox[0] + in.worldWidth;
        out[2] = in.cullBox[2] + in.worldWidth;
    }
}

bool chunkVisible(const Chunk& c, const float box[4]) {
    return c.box[2] >= box[0] && box[2] >= c.box[0] && c.box[3] >= box[1] && box[3] >= c.box[1];
}

Status computeIceFrame(const IceFields& f, const FrameInputs& in, uint32_t copy, uint8_t out[kIceFrameBytes]) {
    if (!out) return Status::fail(Err::BadLayout, "computeIceFrame: out null");
    if (copy >= kMaxCopies) return Status::fail(Err::BadLayout, "computeIceFrame: copy " + std::to_string(copy) + " outside 0..2");
    C5R_TRY(checkFields(f, "computeIceFrame"));
    if (!finite16(in.cam.view) || !finite16(in.cam.proj) || !finite16(in.overlayProj))
        return Status::fail(Err::BadLayout, "computeIceFrame: a matrix is not finite");
    for (float v : in.fowAnim)
        if (!std::isfinite(v)) return Status::fail(Err::BadLayout, "computeIceFrame: FOWAnim not finite");
    for (float v : in.fog.color)
        if (!std::isfinite(v)) return Status::fail(Err::BadLayout, "computeIceFrame: fog colour not finite");
    if (!std::isfinite(in.fog.density) || !std::isfinite(in.fog.scale) || !std::isfinite(in.fog.w) ||
        (in.haveFogCamZ && !std::isfinite(in.fogCamZ)) ||
        (in.haveCamPos && !(std::isfinite(in.camPos[0]) && std::isfinite(in.camPos[1]) && std::isfinite(in.camPos[2]))))
        return Status::fail(Err::BadLayout, "computeIceFrame: the fog or the camera position is not finite");
    if (copy > 0 && !(std::isfinite(in.worldWidth) && in.worldWidth > 0.0f))
        return Status::fail(Err::BadLayout, "computeIceFrame: worldWidth must be > 0 for the map copies");
    float cam[3];
    if (in.haveCamPos) {
        std::memcpy(cam, in.camPos, sizeof cam);
    } else {
        float inv[16];
        if (!inverse44(in.cam.view, inv)) return Status::fail(Err::BadLayout, "computeIceFrame: view not invertible");
        cam[0] = inv[12];
        cam[1] = inv[13];
        cam[2] = inv[14];
    }
    const float W = in.worldWidth;
    const float shift = copy == 1 ? W : (copy == 2 ? -W : 0.0f);
    std::memset(out, 0, kIceFrameBytes);

    // rows 0-3: (T_k * V) * P; for k = 0 the view itself (copied, not multiplied by the identity)
    float view[16], vp[16];
    if (copy == 0) {
        std::memcpy(view, in.cam.view, sizeof view);
    } else {
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, shift, 0, 0, 1};
        mul44f(t, in.cam.view, view);
    }
    mul44f(view, in.cam.proj, vp);
    put(out, 0x000, vp, 16);
    // rows 4, 5: the whole mesh's box
    std::memcpy(out + 0x040, f.raw + (field::kBox - kFieldsBase), 8);
    std::memcpy(out + 0x050, f.raw + (field::kBox + 8 - kFieldsBase), 8);
    // rows 6, 7 (SSE)
    put1(out, 0x060, f.get(field::kHeightAbove) + f.get(field::kDepthBelow));
    put1(out, 0x070, f.get(field::kWaterHeight) - f.get(field::kDepthBelow));
    // rows 8-11: T_k * OverlayProj (multiplied for k = 0 too, by the identity)
    {
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, shift, 0, 0, 1};
        float m[16];
        mul44f(t, in.overlayProj, m);
        put(out, 0x080, m, 16);
    }
    // row 12: the copy's camera position: (cx, cy, cz), (-W + cx, ...), (cx + W, ...)
    {
        float c[3] = {cam[0], cam[1], cam[2]};
        if (copy == 1) c[0] = -W + cam[0];
        if (copy == 2) c[0] = cam[0] + W;
        put(out, 0x0C0, c, 3);
    }
    put(out, 0x0D0, in.fowAnim, 4);
    auto copyField = [&](uint32_t dst, uint32_t src, uint32_t count) {
        std::memcpy(out + dst, f.raw + (src - kFieldsBase), count * 4);
    };
    copyField(0x0E0, field::kDirA, 3);
    copyField(0x0F0, field::kDirB, 3);
    copyField(0x100, field::kLeanScale, 1);
    copyField(0x110, field::kTextureRepeat, 1);
    copyField(0x120, field::kBumpRepeat, 1);
    copyField(0x130, field::kSpecularPower, 1);
    copyField(0x140, field::kSubsurface, 3);
    copyField(0x150, field::kWaterHeight, 1);
    copyField(0x160, field::kSideTexRepeat, 2);
    copyField(0x170, field::kSideBumpRepeat, 2);
    copyField(0x180, field::kUnderwater1, 3);
    copyField(0x190, field::kUnderwater2, 3);
    copyField(0x1A0, field::kColorRamp, 1);
    copyField(0x1B0, field::kAlphaRamp, 1);
    put(out, 0x1C0, in.fog.color, 4);
    // row 29: (-density, exp(-(z * d)) * (-1 / d) * scale in x87 precision, 0, zoom fraction); z is the scene camera's
    const float z = in.haveFogCamZ ? in.fogCamZ : cam[2];
    put1(out, 0x1D0, -in.fog.density);
    {
        const double d = static_cast<double>(in.fog.density);
        const double x = -(static_cast<double>(z) * d);
        const double e = std::exp2(x * 1.4426950408889634);
        put1(out, 0x1D4, static_cast<float>(e * (-1.0 / d) * static_cast<double>(in.fog.scale)));
    }
    put1(out, 0x1D8, 0.0f);
    put1(out, 0x1DC, in.fog.w < 0.0f ? water::zoomFraction(z) : in.fog.w);
    return Status::Ok();
}

const D3D11_INPUT_ELEMENT_DESC* iceInputLayout(uint32_t* count) {
    if (count) *count = 2;
    return kLayout;
}

const char* textureFile(uint32_t slot) { return slot < 7 ? kTextureFiles[slot] : ""; }

// ---- Renderer ----

struct Renderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11SamplerState* samp[11] = {};
    ID3D11ShaderResourceView* own[7] = {};
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11BlendState* bs = nullptr;

    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    std::vector<Chunk> chunks;
    IceFields fields;
    std::vector<std::vector<uint8_t>> frames;

    void dropMesh() {
        rel(vb);
        rel(ib);
        chunks.clear();
        fields = IceFields();
    }
    ~Impl() {
        dropMesh();
        rel(vs);
        rel(ps);
        rel(layout);
        rel(cbFrame);
        for (auto& s : samp) rel(s);
        for (auto& s : own) rel(s);
        rel(rs);
        rel(dss);
        rel(bs);
        rel(dev);
    }

    // A DDS of the install as stock binds it: 2D (or a cube when `cube`), the resource typeless and the view sRGB.
    Status loadDds(const gamefs::GameFiles& fs, const char* name, bool cube, ID3D11ShaderResourceView** out) {
        std::vector<uint8_t> bytes;
        Status st = fs.read(name, bytes);
        if (!st.ok()) return Status::fail(Err::NotFound, std::string(name) + ": " + st.str());
        gamefs::DdsImage img;
        st = gamefs::parseDds(bytes.data(), bytes.size(), img);
        if (!st.ok()) return Status::fail(st.code, std::string(name) + ": " + st.msg);
        if (img.dim != gamefs::DdsDim::Tex2D || img.cube != cube || img.arraySize != (cube ? 6u : 1u))
            return Status::fail(Err::Unsupported, std::string(name) + (cube ? ": not a cube map" : ": not a plain 2D texture"));
        const DXGI_FORMAT tl = typelessOf(img.format);
        if (tl == DXGI_FORMAT_UNKNOWN)
            return Status::fail(Err::Unsupported, std::string(name) + ": no sRGB view for " + gamefs::dxgiFormatName(img.format));
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = img.width;
        d.Height = img.height;
        d.MipLevels = img.mipLevels;
        d.ArraySize = img.arraySize;
        d.Format = tl;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        for (const gamefs::DdsSubresource& s : img.subresources)
            init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + s.offset, s.rowPitch, s.slicePitch});
        ID3D11Texture2D* t = nullptr;
        C5R_TRY(hrOk(dev->CreateTexture2D(&d, init.data(), &t), std::string("CreateTexture2D ") + name));
        D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = srgbOf(img.format);
        if (cube) {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            v.TextureCube.MipLevels = img.mipLevels;
        } else {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = img.mipLevels;
        }
        st = hrOk(dev->CreateShaderResourceView(t, &v, out), std::string("CreateShaderResourceView ") + name);
        t->Release();
        return st;
    }

    // A .lean pair as stock binds the ice's: the four-channel block R8G8B8A8_UNORM, the one-channel block A8_UNORM [L
    // draws 86 / 171]; the 8-bit files only (the ice's two; a 16-bit file is Unsupported: stock's ice was not seen with
    // one).
    Status loadLean(const gamefs::GameFiles& fs, const char* name, ID3D11ShaderResourceView** four,
                    ID3D11ShaderResourceView** one) {
        std::vector<uint8_t> bytes;
        terrainsets::LeanFile lf;
        Status st = terrainsets::loadLean(fs, name, bytes, lf);
        if (!st.ok()) {
            if (!fs.exists(name)) return Status::fail(Err::NotFound, std::string(name) + ": " + st.str());
            return Status::fail(st.code, std::string(name) + ": " + st.msg);
        }
        if (lf.bytesPerChannel != 1)
            return Status::fail(Err::Unsupported, std::string(name) + ": " + std::to_string(lf.bytesPerChannel) +
                                                      " bytes per channel (the ice's files have 1)");
        ID3D11ShaderResourceView** outs[2] = {four, one};
        for (int k = 0; k < 2; ++k) {
            std::vector<D3D11_SUBRESOURCE_DATA> init(lf.mips);
            for (uint32_t m = 0; m < lf.mips; ++m) {
                terrainsets::LeanMip lm;
                C5R_TRY(terrainsets::leanMip(lf, k == 0, m, lm));
                init[m].pSysMem = lm.data;
                init[m].SysMemPitch = lm.rowPitch;
                init[m].SysMemSlicePitch = lm.rowPitch * lm.height;
            }
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = lf.width;
            d.Height = lf.height;
            d.MipLevels = lf.mips;
            d.ArraySize = 1;
            d.Format = k == 0 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ID3D11Texture2D* t = nullptr;
            C5R_TRY(hrOk(dev->CreateTexture2D(&d, init.data(), &t), std::string("CreateTexture2D ") + name));
            st = hrOk(dev->CreateShaderResourceView(t, nullptr, outs[k]), std::string("CreateShaderResourceView ") + name);
            t->Release();
            C5R_TRY(st);
        }
        return Status::Ok();
    }
};

Renderer::Renderer() {}
Renderer::~Renderer() { release(); }
void Renderer::release() {
    delete p_;
    p_ = nullptr;
}
bool Renderer::ready() const { return p_ != nullptr; }

Status Renderer::init(ID3D11Device* device, const shaders::ShaderSection& sec, const gamefs::GameFiles& fs,
                      const InitOptions& o) {
    release();
    if (!device) return Status::fail(Err::BadLayout, "init: no device");
    // the programs: found, of the right stage, declared as stock's ice draw binds them
    const shaders::ShaderBlob* bv = sec.findByVa(o.vsVa);
    if (!bv) return Status::fail(Err::NotFound, "IceShader VS blob " + hex(o.vsVa) + " is not in the .shader section");
    if (bv->stage != shaders::Stage::Vertex)
        return Status::fail(Err::BadLayout, "IceShader VS blob " + hex(o.vsVa) + " is a " + shaders::stageName(bv->stage) + " program");
    const shaders::ShaderBlob* bp = sec.findByVa(o.psVa);
    if (!bp) return Status::fail(Err::NotFound, "IceShader PS blob " + hex(o.psVa) + " is not in the .shader section");
    if (bp->stage != shaders::Stage::Pixel)
        return Status::fail(Err::BadLayout, "IceShader PS blob " + hex(o.psVa) + " is a " + shaders::stageName(bp->stage) + " program");
    shaders::ShaderReflection rv, rp;
    C5R_TRY(shaders::reflect(bv->bytes, bv->size, rv));
    C5R_TRY(shaders::reflect(bp->bytes, bp->size, rp));
    C5R_TRY(checkVs(rv, o.vsVa));
    C5R_TRY(checkPs(rp, o.psVa));

    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    device->AddRef();
    C5R_TRY(hrOk(device->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (IceShader)"));
    C5R_TRY(hrOk(device->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (IceShader)"));
    C5R_TRY(hrOk(device->CreateInputLayout(kLayout, 2, bv->bytes, bv->size, &p->layout), "CreateInputLayout (ice)"));
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = kIceFrameBytes;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(device->CreateBuffer(&d, nullptr, &p->cbFrame), "CreateBuffer (IceFrame)"));
    }
    // samplers [L draws 86 / 171]: s0-s9 MIN_MAG_MIP_LINEAR WRAP, s10 MIN_MAG_MIP_LINEAR CLAMP, LOD 0..max, no bias
    for (uint32_t i = 0; i < 11; ++i) {
        D3D11_SAMPLER_DESC d = {};
        d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        const D3D11_TEXTURE_ADDRESS_MODE a = i == 10 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        d.AddressU = d.AddressV = d.AddressW = a;
        d.MaxAnisotropy = 1;
        d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        d.MinLOD = 0;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(device->CreateSamplerState(&d, &p->samp[i]), "CreateSamplerState (ice)"));
    }
    // states [L]: solid, cull back, front counter-clockwise, depth clip; depth LESS_EQUAL with write, no stencil;
    // blend ONE / INV_SRC_ALPHA (premultiplied), alpha ONE / ZERO, write all
    {
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_BACK;
        r.FrontCounterClockwise = TRUE;
        r.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(device->CreateRasterizerState(&r, &p->rs), "CreateRasterizerState (ice)"));
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dss), "CreateDepthStencilState (ice)"));
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->bs), "CreateBlendState (ice)"));
    }
    // textures t0-t6 from the install (polar-ice.md 1 I8: Ice_D from VT when present)
    C5R_TRY(p->loadDds(fs, kTextureFiles[0], true, &p->own[0]));
    C5R_TRY(p->loadDds(fs, kTextureFiles[1], false, &p->own[1]));
    C5R_TRY(p->loadLean(fs, kTextureFiles[2], &p->own[2], &p->own[3]));
    C5R_TRY(p->loadDds(fs, kTextureFiles[4], false, &p->own[4]));
    C5R_TRY(p->loadLean(fs, kTextureFiles[5], &p->own[5], &p->own[6]));
    p_ = p.release();
    return Status::Ok();
}

Status Renderer::setMesh(const Mesh& m) {
    if (!p_) return Status::fail(Err::NotFound, "setMesh: init not done");
    C5R_TRY(checkFields(m.fields, "setMesh"));
    {
        const float b[4] = {m.fields.get(field::kBox), m.fields.get(field::kBox + 4), m.fields.get(field::kBox + 8),
                            m.fields.get(field::kBox + 12)};
        if (!(b[0] <= b[2] && b[1] <= b[3])) return Status::fail(Err::BadLayout, "setMesh: the mesh box +0xFF1C has min > max");
    }
    if (m.vb.empty() || m.vb.size() % kIceStride)
        return Status::fail(Err::BadLayout, "setMesh: VB of " + std::to_string(m.vb.size()) + " bytes is not whole 8-byte vertices");
    if (m.ib.empty() || m.ib.size() % 2)
        return Status::fail(Err::BadLayout, "setMesh: IB of " + std::to_string(m.ib.size()) + " bytes is not whole R16 indices");
    if (m.chunks.empty()) return Status::fail(Err::BadLayout, "setMesh: zero chunks");
    if (m.vb.size() > 0xFFFFFFFFu || m.ib.size() > 0xFFFFFFFFu) return Status::fail(Err::BadLayout, "setMesh: a buffer above 4 GB");
    const uint64_t vertices = m.vb.size() / kIceStride, indexCount = m.ib.size() / 2;
    const uint16_t* idx = reinterpret_cast<const uint16_t*>(m.ib.data());
    for (size_t c = 0; c < m.chunks.size(); ++c) {
        const Chunk& k = m.chunks[c];
        const std::string who = "setMesh: chunk " + std::to_string(c) + ": ";
        for (float v : k.box)
            if (!std::isfinite(v)) return Status::fail(Err::BadLayout, who + "box not finite");
        if (!(k.box[0] <= k.box[2] && k.box[1] <= k.box[3])) return Status::fail(Err::BadLayout, who + "box min > max");
        if (k.triangles == 0) return Status::fail(Err::BadLayout, who + "0 triangles");
        if (k.baseVertex < 0) return Status::fail(Err::BadLayout, who + "negative base vertex");
        const uint64_t end = static_cast<uint64_t>(k.startIndex) + static_cast<uint64_t>(k.triangles) * 3;
        if (end > indexCount)
            return Status::fail(Err::BadLayout, who + "indices " + std::to_string(k.startIndex) + ".." + std::to_string(end) +
                                                    " lie past the IB's " + std::to_string(indexCount));
        uint32_t maxIdx = 0;
        for (uint64_t i = k.startIndex; i < end; ++i) {
            uint16_t v;
            std::memcpy(&v, idx + i, 2);
            maxIdx = std::max<uint32_t>(maxIdx, v);
        }
        if (static_cast<uint64_t>(maxIdx) + static_cast<uint64_t>(k.baseVertex) >= vertices)
            return Status::fail(Err::BadLayout, who + "index " + std::to_string(maxIdx) + " + base vertex " +
                                                    std::to_string(k.baseVertex) + " lies past the VB's " +
                                                    std::to_string(vertices) + " vertices");
    }
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    D3D11_BUFFER_DESC d = {};
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.ByteWidth = static_cast<UINT>(m.vb.size());
    d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA s = {m.vb.data(), 0, 0};
    C5R_TRY(hrOk(p_->dev->CreateBuffer(&d, &s, &vb), "CreateBuffer (ice VB)"));
    d.ByteWidth = static_cast<UINT>(m.ib.size());
    d.BindFlags = D3D11_BIND_INDEX_BUFFER;
    s.pSysMem = m.ib.data();
    Status st = hrOk(p_->dev->CreateBuffer(&d, &s, &ib), "CreateBuffer (ice IB)");
    if (!st.ok()) {
        rel(vb);
        return st;
    }
    p_->dropMesh();
    p_->vb = vb;
    p_->ib = ib;
    p_->chunks = m.chunks;
    p_->fields = m.fields;
    return Status::Ok();
}

bool Renderer::hasMesh() const { return p_ && p_->vb != nullptr; }
void Renderer::clearMesh() {
    if (p_) p_->dropMesh();
}
uint32_t Renderer::chunkCount() const { return p_ ? static_cast<uint32_t>(p_->chunks.size()) : 0u; }

ID3D11ShaderResourceView* Renderer::ownTexture(uint32_t slot) const { return p_ && slot < 7 ? p_->own[slot] : nullptr; }

const std::vector<std::vector<uint8_t>>& Renderer::frames() const {
    static const std::vector<std::vector<uint8_t>> none;
    return p_ ? p_->frames : none;
}

Status Renderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                      const FrameInputs& in, FrameStats* stats) {
    const auto tStart = std::chrono::steady_clock::now();
    if (!p_) return Status::fail(Err::NotFound, "draw: init not done");
    if (!p_->vb) return Status::fail(Err::NotFound, "draw: no ice mesh set (setMesh)");
    if (!in.fogRamp) return Status::fail(Err::NotFound, "draw: t7 the distance-fog ramp is not given");
    if (!in.fowMask) return Status::fail(Err::NotFound, "draw: t8 the FOW mask is not given");
    if (!in.cloudBase) return Status::fail(Err::NotFound, "draw: t9 the cloud base is not given");
    if (!in.fowLut) return Status::fail(Err::NotFound, "draw: t10 the TerrainFOW LUT is not given");
    if (!ctx || !rtv) return Status::fail(Err::BadLayout, "draw: no context or render target");
    if (in.cam.width == 0 || in.cam.height == 0) return Status::fail(Err::BadLayout, "draw: camera width or height 0");
    if (in.haveCullBox)
        for (float v : in.cullBox)
            if (!std::isfinite(v)) return Status::fail(Err::BadLayout, "draw: cull box not finite");
    Impl& p = *p_;
    const uint32_t copies = in.wrapX ? 3u : 1u;
    // every IceFrame first: a bad input draws nothing
    std::vector<std::vector<uint8_t>> frames(copies, std::vector<uint8_t>(kIceFrameBytes, 0));
    for (uint32_t k = 0; k < copies; ++k) C5R_TRY(computeIceFrame(p.fields, in, k, frames[k].data()));
    p.frames.swap(frames);

    FrameStats st;
    st.copies = copies;
    D3D11_VIEWPORT vpt = {0, 0, static_cast<float>(in.cam.width), static_cast<float>(in.cam.height), 0, 1};
    ctx->RSSetViewports(1, &vpt);
    ctx->RSSetState(p.rs);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetDepthStencilState(p.dss, 0);
    ctx->OMSetBlendState(p.bs, nullptr, 0xFFFFFFFFu);
    ctx->IASetInputLayout(p.layout);
    const UINT stride = kIceStride, off = 0;
    ctx->IASetVertexBuffers(0, 1, &p.vb, &stride, &off);
    ctx->IASetIndexBuffer(p.ib, DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(p.ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = {nullptr, p.cbFrame};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetSamplers(0, 11, p.samp);
    ID3D11ShaderResourceView* srv[11] = {p.own[0], p.own[1], p.own[2], p.own[3], p.own[4], p.own[5], p.own[6],
                                         in.fogRamp, in.fowMask, in.cloudBase, in.fowLut};
    ctx->PSSetShaderResources(0, 11, srv);
    for (uint32_t k = 0; k < copies; ++k) {
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(p.cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (IceFrame)"));
        std::memcpy(m.pData, p.frames[k].data(), kIceFrameBytes);
        ctx->Unmap(p.cbFrame, 0);
        float box[4];
        if (in.haveCullBox) copyCullBox(in, k, box);
        for (const Chunk& c : p.chunks) {
            if (in.haveCullBox && !chunkVisible(c, box)) {
                ++st.chunksCulled;
                continue;
            }
            ctx->DrawIndexed(c.triangles * 3, c.startIndex, c.baseVertex);
            ++st.draws;
            st.indices += static_cast<uint64_t>(c.triangles) * 3;
        }
    }
    st.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    if (stats) *stats = st;
    return Status::Ok();
}

}} // namespace c5r::ice
