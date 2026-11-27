// draw.cpp - c5r::forest: the forest and jungle trees drawn with the EXE's ForestShader programs (epic 0431, ticket 4551).
// Own code, written from the behaviour described in research/05-graphics-composition/trees.md sections 2.1, 2.2, 2.4 and
// 11 (the render setup 0x63A7A0's cb1 stores, the draw objects of 0x6C1CD0, the file table 0x780D80; D83: no decompiler
// or listing text here). The EXE's shaders, the `_2` models and textures are read from the player's files at run time;
// nothing of them is copied here.
//
// The arithmetic of computeForestFrame follows the setup's: single precision where it uses SSE (D3DXMatrixMultiply's
// scalar order), extended precision where it uses the x87 unit (row 14.y, here in double), rounded to float where it
// stores (as c5r::ice's IceFrame row 29).
#include "c5r/forest/draw.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include "c5models/gr2.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/reflect.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/water/water.h"

namespace c5r { namespace forest {

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

bool finiteN(const float* m, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// out = a * b (row-major), each element summed left to right in float (D3DXMatrixMultiply's scalar form, as
// c5r::water's WaterFrame and c5r::ice's IceFrame).
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

// The setup's stores into cb1 (byte offset, float count); trees.md 11.1.
struct Run {
    uint32_t off, count;
};
const Run kStored[] = {
    {0x00, 16},  // rows 0-3   g_View[v] * g_Proj
    {0x40, 16},  // rows 4-7   OverlayProj[v]
    {0x80, 4},   // row 8      FOWAnim
    {0x90, 2},   // row 9.xy   the target's width, height
    {0xA0, 1},   // row 10.x   overlay_alpha
    {0xB0, 3},   // row 11.xyz the shadow direction
    {0xC0, 3},   // row 12.xyz the view's camera position
    {0xD0, 4},   // row 13     DistanceFog colour
    {0xE0, 4},   // row 14     (-density, fog term, 0, zoom fraction)
};

const char* const kStyleName[kEntries] = {"europe", "america", "asia", "africa", "europe"};
const char* const kKindName[kKinds] = {"forest", "jungle"};
const uint32_t kLutOfEntry[kEntries] = {0, 3, 2, 4, 1};

struct Names {
    std::string model[kEntries][kKinds], diffuse[kEntries][kKinds], overlay[kEntries][kKinds];
    Names() {
        for (uint32_t e = 0; e < kEntries; ++e)
            for (uint32_t k = 0; k < kKinds; ++k) {
                const std::string base = std::string(kKindName[k]) + "_" + kStyleName[e] + "_2";
                model[e][k] = base + ".gr2";
                diffuse[e][k] = base + ".dds";
                overlay[e][k] = std::string(kKindName[k]) + "_overlay_" + kStyleName[e] + "_2.dds";
            }
    }
};
const Names& names() {
    static const Names n;
    return n;
}

const D3D11_INPUT_ELEMENT_DESC kLayout[4] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// The PS's resource declarations stock's draw binds for (trees.md 11.1): slot -> D3D10_SB_RESOURCE_DIMENSION.
uint32_t expectedPsDim(uint32_t slot) {
    switch (slot) {
    case 0: case 1: case 3: case 4: case 6: case 7: return 3;   // texture2d
    case 5: case 8: return 5;                                   // texture3d
    default: return 0;                                          // t2 is bound but must not be declared
    }
}

Status checkVs(const shaders::ShaderReflection& r, uint32_t va) {
    const std::string who = "ForestShader VS " + hex(va) + ": ";
    const auto& e = r.input.elems;
    const bool ok = e.size() == 4 && e[0].semantic == "POSITION" && e[0].semanticIndex == 0 && e[0].reg == 0 &&
                    e[0].componentType == 3 && (e[0].mask & 7) == 7 && e[1].semantic == "NORMAL" && e[1].reg == 1 &&
                    e[1].componentType == 3 && e[2].semantic == "TEXCOORD" && e[2].semanticIndex == 0 && e[2].reg == 2 &&
                    e[2].componentType == 3 && (e[2].mask & 3) == 3 && e[3].semantic == "BLENDINDICES" &&
                    e[3].semanticIndex == 0 && e[3].reg == 3 && e[3].componentType == 1;
    if (!ok)
        return Status::fail(Err::BadLayout, who + "input declaration is not POSITION.xyz + NORMAL.xyz + TEXCOORD0.xy (float) "
                                                  "+ BLENDINDICES (uint): " + shaders::sigSummary(r.input));
    bool cb1 = false, cb7 = false;
    for (const shaders::CbDecl& c : r.cbuffers) {
        if (c.slot == 1) {
            if (c.vec4Count == 0 || c.vec4Count * 16 > kFrameBytes)
                return Status::fail(Err::BadLayout, who + "cb1 has " + std::to_string(c.vec4Count) + " rows; the tree frame has 15");
            cb1 = true;
        } else if (c.slot == 7) {
            if (c.vec4Count != kSlots)
                return Status::fail(Err::BadLayout, who + "cb7 has " + std::to_string(c.vec4Count) + " rows; a block has 90 trees");
            cb7 = true;
        } else {
            return Status::fail(Err::BadLayout, who + "declares cb" + std::to_string(c.slot) + " (stock binds cb1 and cb7)");
        }
    }
    if (!cb1 || !cb7) return Status::fail(Err::BadLayout, who + "does not declare cb1 and cb7");
    if (!r.resources.empty()) return Status::fail(Err::BadLayout, who + "declares a resource (stock binds none to the VS)");
    return Status::Ok();
}

Status checkPs(const shaders::ShaderReflection& r, uint32_t va) {
    const std::string who = "ForestShader PS " + hex(va) + ": ";
    if (r.cbuffers.size() != 1 || r.cbuffers[0].slot != 1)
        return Status::fail(Err::BadLayout, who + "constant buffers are not cb1 alone");
    if (r.cbuffers[0].vec4Count * 16 != kFrameBytes)
        return Status::fail(Err::BadLayout, who + "cb1 has " + std::to_string(r.cbuffers[0].vec4Count) +
                                                " rows; the tree frame is 15 (240 bytes)");
    uint32_t seen = 0;
    for (const shaders::ResourceDecl& d : r.resources) {
        if (d.uav || d.slot >= 16 || expectedPsDim(d.slot) == 0)
            return Status::fail(Err::BadLayout, who + "declares slot " + std::string(d.uav ? "u" : "t") + std::to_string(d.slot) +
                                                    ", which stock's tree draw does not bind for it");
        if (d.dimension != expectedPsDim(d.slot))
            return Status::fail(Err::BadLayout, who + "t" + std::to_string(d.slot) + " is a " +
                                                    shaders::resourceDimName(d.dimension) + ", not a " +
                                                    shaders::resourceDimName(expectedPsDim(d.slot)));
        seen |= 1u << d.slot;
    }
    const uint32_t want = 0x1FBu;  // t0, t1, t3-t8
    if (seen != want) return Status::fail(Err::BadLayout, who + "does not declare every slot of t0, t1, t3-t8");
    for (const shaders::SamplerDecl& s : r.samplers)
        if (s.slot > 8) return Status::fail(Err::BadLayout, who + "declares sampler s" + std::to_string(s.slot) + " (stock binds s0-s8)");
    return Status::Ok();
}

} // namespace

// ---- names, rand, cards ----

uint32_t lutIndex(uint32_t entry) { return entry < kEntries ? kLutOfEntry[entry] : kLuts; }
const char* modelFile(uint32_t e, uint32_t k) { return e < kEntries && k < kKinds ? names().model[e][k].c_str() : ""; }
const char* diffuseFile(uint32_t e, uint32_t k) { return e < kEntries && k < kKinds ? names().diffuse[e][k].c_str() : ""; }
const char* overlayFile(uint32_t e, uint32_t k) { return e < kEntries && k < kKinds ? names().overlay[e][k].c_str() : ""; }

Status cardsFromGr2(const uint8_t* data, size_t size, const std::string& name, std::vector<Card>& out) {
    out.clear();
    c5m::FileInfo fi;
    c5m::Error err;
    if (!c5m::read_gr2(data, size, nullptr, fi, &err)) {
        Err code = Err::BadLayout;
        if (err.code == c5m::Err::Truncated) code = Err::Truncated;
        else if (err.code == c5m::Err::BadMagic) code = Err::BadMagic;
        else if (err.code == c5m::Err::OutOfMemory) code = Err::OutOfMemory;
        return Status::fail(code, name + ": " + c5m::err_name(err.code) + " " + err.what);
    }
    if (fi.meshes.empty()) return Status::fail(Err::BadLayout, name + ": no mesh");
    std::vector<Card> cards;
    for (const c5m::Mesh& m : fi.meshes) {
        const std::string who = name + ": mesh " + m.name + ": ";
        const c5m::VertexComponent* p = m.format.find("Position");
        const c5m::VertexComponent* n = m.format.find("Normal");
        const c5m::VertexComponent* t = m.format.find("TextureCoordinates0");
        if (!p || p->type != c5m::MemberType::Real32 || p->count != 3 || !n || n->type != c5m::MemberType::Real32 ||
            n->count != 3 || !t || t->type != c5m::MemberType::Real32 || t->count < 2 || t->count > 4)
            return Status::fail(Err::BadLayout, who + "not Real32 Position[3], Normal[3], TextureCoordinates0[2..4]: " + m.format.key());
        if (m.vertex_count == 0) return Status::fail(Err::BadLayout, who + "no vertices");
        if (m.vertex_count > 65536u / kSlots)
            return Status::fail(Err::BadLayout, who + std::to_string(m.vertex_count) + " vertices: 90 cards would not fit 16-bit indices");
        if (m.indices.empty() || m.indices.size() % 3)
            return Status::fail(Err::BadLayout, who + std::to_string(m.indices.size()) + " indices (not whole triangles)");
        Card c;
        c.name = m.name;
        c.vertices.resize(static_cast<size_t>(m.vertex_count) * 8);
        for (uint32_t v = 0; v < m.vertex_count; ++v) {
            float a[4], b[4], uv[4];
            if (m.read(v, "Position", a) != 3 || m.read(v, "Normal", b) != 3 || m.read(v, "TextureCoordinates0", uv) < 2)
                return Status::fail(Err::BadLayout, who + "vertex " + std::to_string(v) + " not readable");
            float* o = &c.vertices[static_cast<size_t>(v) * 8];
            o[0] = a[0]; o[1] = a[1]; o[2] = a[2];
            o[3] = b[0]; o[4] = b[1]; o[5] = b[2];
            o[6] = uv[0]; o[7] = uv[1];
            if (!finiteN(o, 8)) return Status::fail(Err::BadLayout, who + "vertex " + std::to_string(v) + " not finite");
        }
        for (uint32_t i : m.indices)
            if (i >= m.vertex_count)
                return Status::fail(Err::BadLayout, who + "index " + std::to_string(i) + " past its " +
                                                        std::to_string(m.vertex_count) + " vertices");
        c.indices = m.indices;
        cards.push_back(std::move(c));
    }
    out.swap(cards);
    return Status::Ok();
}

void chooseCards(CrtRand& r, const uint32_t nForest[kEntries], const uint32_t nJungle[kEntries],
                 uint32_t forest[kEntries][kSlots], uint32_t jungle[kEntries][kSlots]) {
    for (uint32_t e = 0; e < kEntries; ++e)
        for (uint32_t k = 0; k < kSlots; ++k) {
            const uint32_t a = static_cast<uint32_t>(r.next());
            forest[e][k] = nForest[e] ? a % nForest[e] : 0;
            const uint32_t b = static_cast<uint32_t>(r.next());
            jungle[e][k] = nJungle[e] ? b % nJungle[e] : 0;
        }
}

Status buildCardBuffers(const std::vector<Card>& cards, const uint32_t choice[kSlots], CardBuffers& out) {
    if (cards.empty()) return Status::fail(Err::BadLayout, "buildCardBuffers: no cards");
    uint32_t maxV = 0;
    size_t maxI = 0;
    for (size_t c = 0; c < cards.size(); ++c) {
        if (cards[c].vertexCount() == 0 || cards[c].indices.empty())
            return Status::fail(Err::BadLayout, "buildCardBuffers: card " + std::to_string(c) + " has no vertices or indices");
        maxV = std::max(maxV, cards[c].vertexCount());
        maxI = std::max(maxI, cards[c].indices.size());
    }
    for (uint32_t k = 0; k < kSlots; ++k)
        if (choice[k] >= cards.size())
            return Status::fail(Err::BadLayout, "buildCardBuffers: slot " + std::to_string(k) + " chooses card " +
                                                    std::to_string(choice[k]) + " of " + std::to_string(cards.size()));
    if (static_cast<uint64_t>(maxV) * kSlots > 65536u)
        return Status::fail(Err::BadLayout, "buildCardBuffers: more than 65,536 vertices");
    CardBuffers b;
    b.vb.assign(static_cast<size_t>(maxV) * kSlots * kStride, 0);
    b.ib.assign(maxI * kSlots * 2, 0);
    uint32_t base = 0;
    size_t idx = 0;
    for (uint32_t k = 0; k < kSlots; ++k) {
        const Card& c = cards[choice[k]];
        for (uint32_t v = 0; v < c.vertexCount(); ++v) {
            uint8_t* o = b.vb.data() + static_cast<size_t>(base + v) * kStride;
            std::memcpy(o, &c.vertices[static_cast<size_t>(v) * 8], kCardVertexBytes);
            o[32] = static_cast<uint8_t>(k);
        }
        for (uint32_t i : c.indices) {
            const uint16_t x = static_cast<uint16_t>(i + base);
            std::memcpy(b.ib.data() + idx * 2, &x, 2);
            ++idx;
        }
        base += c.vertexCount();
    }
    b.indexCount = static_cast<uint32_t>(idx);
    out = std::move(b);
    return Status::Ok();
}

// ---- cb1 (trees.md 11.1) ----

bool forestFrameStored(uint32_t off) {
    for (const Run& r : kStored)
        if (off >= r.off && off < r.off + r.count * 4) return true;
    return false;
}

Status computeForestFrame(const FrameInputs& in, uint32_t v, uint8_t out[kFrameBytes]) {
    if (!out) return Status::fail(Err::BadLayout, "computeForestFrame: out null");
    if (in.numViews == 0 || in.numViews > kMaxViews)
        return Status::fail(Err::BadLayout, "computeForestFrame: numViews " + std::to_string(in.numViews) + " outside 1..4");
    if (v >= in.numViews)
        return Status::fail(Err::BadLayout, "computeForestFrame: view " + std::to_string(v) + " of " + std::to_string(in.numViews));
    if (in.width == 0 || in.height == 0) return Status::fail(Err::BadLayout, "computeForestFrame: width or height 0");
    const View& w = in.views[v];
    if (!finiteN(w.view, 16) || !finiteN(in.proj, 16) || !finiteN(w.overlayProj, 16))
        return Status::fail(Err::BadLayout, "computeForestFrame: a matrix is not finite");
    if (!finiteN(in.fowAnim, 4)) return Status::fail(Err::BadLayout, "computeForestFrame: FOWAnim not finite");
    if (!finiteN(in.fog.color, 4) || !std::isfinite(in.fog.density) || !std::isfinite(in.fog.scale) || !std::isfinite(in.fog.w))
        return Status::fail(Err::BadLayout, "computeForestFrame: the fog is not finite");
    if (in.fog.density == 0.0f) return Status::fail(Err::BadLayout, "computeForestFrame: fog density 0");
    if (!std::isfinite(in.overlayAlpha) || !finiteN(in.auxDir, 3))
        return Status::fail(Err::BadLayout, "computeForestFrame: overlay_alpha or the shadow direction not finite");
    if (w.haveCamPos && !finiteN(w.camPos, 3))
        return Status::fail(Err::BadLayout, "computeForestFrame: the camera position is not finite");
    float cam[3];
    if (w.haveCamPos) {
        std::memcpy(cam, w.camPos, sizeof cam);
    } else {
        float inv[16];
        if (!inverse44(w.view, inv)) return Status::fail(Err::BadLayout, "computeForestFrame: view not invertible");
        cam[0] = inv[12];
        cam[1] = inv[13];
        cam[2] = inv[14];
    }
    std::memset(out, 0, kFrameBytes);
    float vp[16];
    mul44f(w.view, in.proj, vp);
    put(out, 0x00, vp, 16);
    put(out, 0x40, w.overlayProj, 16);
    put(out, 0x80, in.fowAnim, 4);
    put1(out, 0x90, static_cast<float>(in.width));
    put1(out, 0x94, static_cast<float>(in.height));
    put1(out, 0xA0, in.overlayAlpha);
    put(out, 0xB0, in.auxDir, 3);
    put(out, 0xC0, cam, 3);
    put(out, 0xD0, in.fog.color, 4);
    // row 14: (-density, exp(-(density x z)) x (-1 / density) x scale in x87 precision, 0, zoom fraction)
    put1(out, 0xE0, -in.fog.density);
    {
        const double d = static_cast<double>(in.fog.density);
        const double x = -(d * static_cast<double>(cam[2]));
        const double e = std::exp2(x * 1.4426950408889634);
        put1(out, 0xE4, static_cast<float>(e * (-1.0 / d) * static_cast<double>(in.fog.scale)));
    }
    put1(out, 0xE8, 0.0f);
    put1(out, 0xEC, in.fog.w < 0.0f ? water::zoomFraction(cam[2]) : in.fog.w);
    return Status::Ok();
}

const D3D11_INPUT_ELEMENT_DESC* forestInputLayout(uint32_t* count) {
    if (count) *count = 4;
    return kLayout;
}

// ---- Renderer ----

struct Renderer::Impl {
    ID3D11Device* dev = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11Buffer* cbBlock = nullptr;
    ID3D11SamplerState* samp[9] = {};
    ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11BlendState* bs = nullptr;
    // per entry and kind
    ID3D11ShaderResourceView* tex[kEntries][kKinds][2] = {};
    ID3D11Buffer* vb[kEntries][kKinds] = {};
    ID3D11Buffer* ib[kEntries][kKinds] = {};
    CardBuffers bufs[kEntries][kKinds];
    std::vector<uint32_t> choice[kEntries][kKinds];
    std::vector<Card> cards[kEntries][kKinds];
    std::vector<std::vector<uint8_t>> frames;

    ~Impl() {
        for (uint32_t e = 0; e < kEntries; ++e)
            for (uint32_t k = 0; k < kKinds; ++k) {
                rel(tex[e][k][0]);
                rel(tex[e][k][1]);
                rel(vb[e][k]);
                rel(ib[e][k]);
            }
        rel(vs);
        rel(ps);
        rel(layout);
        rel(cbFrame);
        rel(cbBlock);
        for (auto& s : samp) rel(s);
        rel(rs);
        rel(dss);
        rel(bs);
        rel(dev);
    }

    // A DDS of the install as a plain 2D texture with its full mip chain, viewed in its own format (stock's views of
    // the `_2` set are BC2_UNORM / BC3_UNORM, not sRGB [L]).
    Status loadDds(const gamefs::GameFiles& fs, const std::string& name, ID3D11ShaderResourceView** out) {
        std::vector<uint8_t> bytes;
        if (!fs.exists(name)) return Status::fail(Err::NotFound, name + ": not in the install");
        Status st = fs.read(name, bytes);
        if (!st.ok()) return Status::fail(st.code, name + ": " + st.msg);
        gamefs::DdsImage img;
        st = gamefs::parseDds(bytes.data(), bytes.size(), img);
        if (!st.ok()) return Status::fail(st.code, name + ": " + st.msg);
        if (img.dim != gamefs::DdsDim::Tex2D || img.cube || img.arraySize != 1)
            return Status::fail(Err::Unsupported, name + ": not a plain 2D texture");
        ID3D11Resource* r = nullptr;
        st = gamefs::createTexture(dev, img, &r, out);
        rel(r);
        if (!st.ok()) return Status::fail(st.code, name + ": " + st.msg);
        return Status::Ok();
    }

    Status makeBuffers(uint32_t e, uint32_t k, const std::vector<uint8_t>& vbBytes, const std::vector<uint8_t>& ibBytes) {
        ID3D11Buffer* v = nullptr;
        ID3D11Buffer* i = nullptr;
        D3D11_BUFFER_DESC d = {};
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.ByteWidth = static_cast<UINT>(vbBytes.size());
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s = {vbBytes.data(), 0, 0};
        C5R_TRY(hrOk(dev->CreateBuffer(&d, &s, &v), "CreateBuffer (tree VB)"));
        d.ByteWidth = static_cast<UINT>(ibBytes.size());
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        s.pSysMem = ibBytes.data();
        const Status st = hrOk(dev->CreateBuffer(&d, &s, &i), "CreateBuffer (tree IB)");
        if (!st.ok()) {
            rel(v);
            return st;
        }
        rel(vb[e][k]);
        rel(ib[e][k]);
        vb[e][k] = v;
        ib[e][k] = i;
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
    const shaders::ShaderBlob* bv = sec.findByVa(o.vsVa);
    if (!bv) return Status::fail(Err::NotFound, "ForestShader VS blob " + hex(o.vsVa) + " is not in the .shader section");
    if (bv->stage != shaders::Stage::Vertex)
        return Status::fail(Err::BadLayout, "ForestShader VS blob " + hex(o.vsVa) + " is a " + shaders::stageName(bv->stage) + " program");
    const shaders::ShaderBlob* bp = sec.findByVa(o.psVa);
    if (!bp) return Status::fail(Err::NotFound, "ForestShader PS blob " + hex(o.psVa) + " is not in the .shader section");
    if (bp->stage != shaders::Stage::Pixel)
        return Status::fail(Err::BadLayout, "ForestShader PS blob " + hex(o.psVa) + " is a " + shaders::stageName(bp->stage) + " program");
    shaders::ShaderReflection rv, rp;
    C5R_TRY(shaders::reflect(bv->bytes, bv->size, rv));
    C5R_TRY(shaders::reflect(bp->bytes, bp->size, rp));
    C5R_TRY(checkVs(rv, o.vsVa));
    C5R_TRY(checkPs(rp, o.psVa));

    std::unique_ptr<Impl> p(new Impl());
    p->dev = device;
    device->AddRef();
    C5R_TRY(hrOk(device->CreateVertexShader(bv->bytes, bv->size, nullptr, &p->vs), "CreateVertexShader (ForestShader)"));
    C5R_TRY(hrOk(device->CreatePixelShader(bp->bytes, bp->size, nullptr, &p->ps), "CreatePixelShader (ForestShader)"));
    C5R_TRY(hrOk(device->CreateInputLayout(kLayout, 4, bv->bytes, bv->size, &p->layout), "CreateInputLayout (trees)"));
    {
        D3D11_BUFFER_DESC d = {};
        d.ByteWidth = kFrameBytes;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        C5R_TRY(hrOk(device->CreateBuffer(&d, nullptr, &p->cbFrame), "CreateBuffer (tree cb1)"));
        d.ByteWidth = kBlockBytes;
        C5R_TRY(hrOk(device->CreateBuffer(&d, nullptr, &p->cbBlock), "CreateBuffer (tree cb7)"));
    }
    // samplers [L, trees.md 2.4]: MIN_MAG_MIP_LINEAR, no bias, LOD 0..max, NEVER; wrap s0-s3, s6, s7; clamp s4, s5, s8
    for (uint32_t i = 0; i < 9; ++i) {
        D3D11_SAMPLER_DESC d = {};
        d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        const bool clamp = i == 4 || i == 5 || i == 8;
        const D3D11_TEXTURE_ADDRESS_MODE a = clamp ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        d.AddressU = d.AddressV = d.AddressW = a;
        d.MaxAnisotropy = 1;
        d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        d.MinLOD = 0;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        C5R_TRY(hrOk(device->CreateSamplerState(&d, &p->samp[i]), "CreateSamplerState (trees)"));
    }
    // states [L]: solid, cull back, front counter-clockwise, depth clip; depth LESS_EQUAL with write, no stencil;
    // blend SRC_ALPHA / INV_SRC_ALPHA, alpha ONE / ONE, write all
    {
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_BACK;
        r.FrontCounterClockwise = TRUE;
        r.DepthClipEnable = TRUE;
        C5R_TRY(hrOk(device->CreateRasterizerState(&r, &p->rs), "CreateRasterizerState (trees)"));
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        C5R_TRY(hrOk(device->CreateDepthStencilState(&ds, &p->dss), "CreateDepthStencilState (trees)"));
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        C5R_TRY(hrOk(device->CreateBlendState(&bd, &p->bs), "CreateBlendState (trees)"));
    }
    // the `_2` textures: one load per file (entries 0 and 4 share the Europe files)
    {
        std::map<std::string, ID3D11ShaderResourceView*> loaded;
        for (uint32_t e = 0; e < kEntries; ++e)
            for (uint32_t k = 0; k < kKinds; ++k)
                for (uint32_t s = 0; s < 2; ++s) {
                    const std::string& n = s == 0 ? names().diffuse[e][k] : names().overlay[e][k];
                    auto it = loaded.find(n);
                    if (it == loaded.end()) {
                        ID3D11ShaderResourceView* v = nullptr;
                        C5R_TRY(p->loadDds(fs, n, &v));
                        it = loaded.emplace(n, v).first;
                    } else {
                        it->second->AddRef();
                    }
                    p->tex[e][k][s] = it->second;
                }
    }
    // the cards: one read per model file, then the EXE's 900 rand() calls and the 10 buffer pairs
    {
        std::map<std::string, std::vector<Card>> models;
        for (uint32_t e = 0; e < kEntries; ++e)
            for (uint32_t k = 0; k < kKinds; ++k) {
                const std::string& n = names().model[e][k];
                if (!models.count(n)) {
                    if (!fs.exists(n)) return Status::fail(Err::NotFound, n + ": not in the install");
                    std::vector<uint8_t> bytes;
                    Status st = fs.read(n, bytes);
                    if (!st.ok()) return Status::fail(st.code, n + ": " + st.msg);
                    std::vector<Card> c;
                    C5R_TRY(cardsFromGr2(bytes.data(), bytes.size(), n, c));
                    models.emplace(n, std::move(c));
                }
                p->cards[e][k] = models[n];
            }
        CrtRand r(o.randSeed);
        uint32_t fc[kEntries][kSlots], jc[kEntries][kSlots], nf[kEntries], nj[kEntries];
        for (uint32_t e = 0; e < kEntries; ++e) {
            nf[e] = static_cast<uint32_t>(p->cards[e][kForest].size());
            nj[e] = static_cast<uint32_t>(p->cards[e][kJungle].size());
        }
        chooseCards(r, nf, nj, fc, jc);
        for (uint32_t e = 0; e < kEntries; ++e)
            for (uint32_t k = 0; k < kKinds; ++k) {
                const uint32_t* ch = k == kForest ? fc[e] : jc[e];
                p->choice[e][k].assign(ch, ch + kSlots);
                C5R_TRY(buildCardBuffers(p->cards[e][k], ch, p->bufs[e][k]));
                C5R_TRY(p->makeBuffers(e, k, p->bufs[e][k].vb, p->bufs[e][k].ib));
            }
    }
    p_ = p.release();
    return Status::Ok();
}

Status Renderer::replaceCards(uint32_t e, uint32_t k, const std::vector<uint8_t>& vbBytes, const std::vector<uint8_t>& ibBytes,
                              uint32_t indexCount) {
    if (!p_) return Status::fail(Err::NotFound, "replaceCards: init not done");
    if (e >= kEntries || k >= kKinds) return Status::fail(Err::BadLayout, "replaceCards: entry or kind out of range");
    if (vbBytes.empty() || vbBytes.size() % kStride || vbBytes.size() > 0xFFFFFFFFu)
        return Status::fail(Err::BadLayout, "replaceCards: VB of " + std::to_string(vbBytes.size()) + " bytes is not whole 36-byte vertices");
    if (ibBytes.empty() || ibBytes.size() % 2 || ibBytes.size() > 0xFFFFFFFFu)
        return Status::fail(Err::BadLayout, "replaceCards: IB of " + std::to_string(ibBytes.size()) + " bytes is not whole R16 indices");
    if (indexCount == 0 || indexCount % 3 || indexCount > ibBytes.size() / 2)
        return Status::fail(Err::BadLayout, "replaceCards: index count " + std::to_string(indexCount) + " (IB holds " +
                                                std::to_string(ibBytes.size() / 2) + ")");
    const size_t vertices = vbBytes.size() / kStride;
    for (uint32_t i = 0; i < indexCount; ++i) {
        uint16_t x;
        std::memcpy(&x, ibBytes.data() + 2 * static_cast<size_t>(i), 2);
        if (x >= vertices)
            return Status::fail(Err::BadLayout, "replaceCards: index " + std::to_string(i) + " = " + std::to_string(x) +
                                                    " past the VB's " + std::to_string(vertices) + " vertices");
        if (vbBytes[static_cast<size_t>(x) * kStride + 32] >= kSlots)
            return Status::fail(Err::BadLayout, "replaceCards: vertex " + std::to_string(x) + " has slot " +
                                                    std::to_string(vbBytes[static_cast<size_t>(x) * kStride + 32]) + " (>= 90)");
    }
    C5R_TRY(p_->makeBuffers(e, k, vbBytes, ibBytes));
    p_->bufs[e][k].vb = vbBytes;
    p_->bufs[e][k].ib = ibBytes;
    p_->bufs[e][k].indexCount = indexCount;
    p_->choice[e][k].clear();
    return Status::Ok();
}

ID3D11ShaderResourceView* Renderer::texture(uint32_t e, uint32_t k, uint32_t slot) const {
    return p_ && e < kEntries && k < kKinds && slot < 2 ? p_->tex[e][k][slot] : nullptr;
}
const std::vector<Card>& Renderer::cards(uint32_t e, uint32_t k) const {
    static const std::vector<Card> none;
    return p_ && e < kEntries && k < kKinds ? p_->cards[e][k] : none;
}
const std::vector<uint32_t>& Renderer::choice(uint32_t e, uint32_t k) const {
    static const std::vector<uint32_t> none;
    return p_ && e < kEntries && k < kKinds ? p_->choice[e][k] : none;
}
const CardBuffers& Renderer::buffers(uint32_t e, uint32_t k) const {
    static const CardBuffers none;
    return p_ && e < kEntries && k < kKinds ? p_->bufs[e][k] : none;
}
const std::vector<std::vector<uint8_t>>& Renderer::frames() const {
    static const std::vector<std::vector<uint8_t>> none;
    return p_ ? p_->frames : none;
}

Status Renderer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                      const FrameInputs& in, const Block* blocks, size_t count, FrameStats* stats) {
    const auto tStart = std::chrono::steady_clock::now();
    if (!p_) return Status::fail(Err::NotFound, "draw: init not done");
    if (!in.screenOverlay) return Status::fail(Err::NotFound, "draw: t3 the Screen Overlay is not given");
    if (!in.fogRamp) return Status::fail(Err::NotFound, "draw: t4 the distance-fog ramp is not given");
    if (!in.fowMask) return Status::fail(Err::NotFound, "draw: t6 the FOW mask is not given");
    if (!in.cloudBase) return Status::fail(Err::NotFound, "draw: t7 the cloud base is not given");
    if (!ctx || !rtv) return Status::fail(Err::BadLayout, "draw: no context or render target");
    if (count && !blocks) return Status::fail(Err::BadLayout, "draw: blocks null");
    for (size_t b = 0; b < count; ++b) {
        const Block& k = blocks[b];
        if (k.entry >= kEntries || k.kind >= kKinds)
            return Status::fail(Err::BadLayout, "draw: block " + std::to_string(b) + ": entry " + std::to_string(k.entry) +
                                                    " / kind " + std::to_string(k.kind) + " out of range");
        if (!finiteN(k.trees, static_cast<int>(kBlockFloats)))
            return Status::fail(Err::BadLayout, "draw: block " + std::to_string(b) + ": a value is not finite");
        const uint32_t li = lutIndex(k.entry);
        if (!in.colorKeyLut[li])
            return Status::fail(Err::NotFound, "draw: t5 the Terrain ColorKey LUT " + std::to_string(li) + " (style entry " +
                                                   std::to_string(k.entry) + ") is not given");
        if (!in.fowLut[li])
            return Status::fail(Err::NotFound, "draw: t8 the TerrainFOW LUT " + std::to_string(li) + " (style entry " +
                                                   std::to_string(k.entry) + ") is not given");
    }
    Impl& p = *p_;
    // every cb1 first: a bad input draws nothing
    if (in.numViews == 0 || in.numViews > kMaxViews)
        return Status::fail(Err::BadLayout, "draw: numViews " + std::to_string(in.numViews) + " outside 1..4");
    std::vector<std::vector<uint8_t>> frames(in.numViews, std::vector<uint8_t>(kFrameBytes, 0));
    for (uint32_t v = 0; v < in.numViews; ++v) C5R_TRY(computeForestFrame(in, v, frames[v].data()));
    p.frames.swap(frames);

    FrameStats st;
    st.views = in.numViews;
    D3D11_VIEWPORT vpt = {0, 0, static_cast<float>(in.width), static_cast<float>(in.height), 0, 1};
    ctx->RSSetViewports(1, &vpt);
    ctx->RSSetState(p.rs);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetDepthStencilState(p.dss, 0);
    ctx->OMSetBlendState(p.bs, nullptr, 0xFFFFFFFFu);
    ctx->IASetInputLayout(p.layout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(p.vs, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(p.ps, nullptr, 0);
    ID3D11Buffer* cbs[8] = {nullptr, p.cbFrame, nullptr, nullptr, nullptr, nullptr, nullptr, p.cbBlock};
    ctx->VSSetConstantBuffers(0, 8, cbs);
    ctx->PSSetConstantBuffers(0, 8, cbs);
    ctx->PSSetSamplers(0, 9, p.samp);
    uint32_t boundE = kEntries, boundK = kKinds;
    for (uint32_t v = 0; v < in.numViews; ++v) {
        D3D11_MAPPED_SUBRESOURCE m;
        C5R_TRY(hrOk(ctx->Map(p.cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (tree cb1)"));
        std::memcpy(m.pData, p.frames[v].data(), kFrameBytes);
        ctx->Unmap(p.cbFrame, 0);
        for (size_t b = 0; b < count; ++b) {
            const Block& k = blocks[b];
            if (k.entry != boundE || k.kind != boundK) {
                const uint32_t li = lutIndex(k.entry);
                ID3D11ShaderResourceView* srv[9] = {p.tex[k.entry][k.kind][0], p.tex[k.entry][k.kind][1], in.terrainOverlay,
                                                    in.screenOverlay, in.fogRamp, in.colorKeyLut[li], in.fowMask,
                                                    in.cloudBase, in.fowLut[li]};
                ctx->PSSetShaderResources(0, 9, srv);
                const UINT stride = kStride, off = 0;
                ctx->IASetVertexBuffers(0, 1, &p.vb[k.entry][k.kind], &stride, &off);
                ctx->IASetIndexBuffer(p.ib[k.entry][k.kind], DXGI_FORMAT_R16_UINT, 0);
                boundE = k.entry;
                boundK = k.kind;
            }
            C5R_TRY(hrOk(ctx->Map(p.cbBlock, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "Map (tree cb7)"));
            std::memcpy(m.pData, k.trees, kBlockBytes);
            ctx->Unmap(p.cbBlock, 0);
            const uint32_t n = p.bufs[k.entry][k.kind].indexCount;
            ctx->DrawIndexedInstanced(n, 1, 0, 0, 0);
            ++st.draws;
            st.indices += n;
        }
    }
    st.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    if (stats) *stats = st;
    return Status::Ok();
}

}} // namespace c5r::forest
