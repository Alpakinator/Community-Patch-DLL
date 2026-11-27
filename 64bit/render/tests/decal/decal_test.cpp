// render/tests/decal/decal_test.cpp - c5r::decal::DecalPass (issue 4104, epic 0166): the decal pass and the bake after
// it, compared with the EXE's own output for four captured tiles; negatives for every check.
//
// Inputs. C5R_EXE: the EXE (its shaders). C5R_DECAL_REF (default F:/c5-64bit-work/0166/4104/ref): the four reference
// tiles A_roadfarm, B_river, C_run4_f335, D_run4_f337, each a folder made by mkref_decal.py from 4109's reference set
// (game data, never committed). C5R_DECAL_GAME_DIR (default F:/c5-instances/c5h-g/install, the install the captures
// came from) and C5R_DECAL_MODS (default its MODS/(2) Vox Populi) for the arm that loads the decal textures by name
// through c5r::gamefs. A missing input FAILS the test with its path (a skip counts as a failure, COMMON 9; here it is a
// named failure). Figures: stdout and C5R_WORK/decal/decal_ref.csv; terrain-decals.md section 11.
#include <windows.h>
#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/bake/blend.h"
#include "c5r/bake/post.h"
#include "c5r/decal/decal.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "testkit.h"
#include "testutil.h"

using Microsoft::WRL::ComPtr;
using namespace c5r;
using namespace c5r::decal;

namespace {

std::wstring wide(const std::string& s) { return gamefs::fromUtf8(s); }

std::string env(const char* name, const char* dflt) {
    const char* e = std::getenv(name);
    return e && *e ? e : dflt;
}
std::string refRoot() { return env("C5R_DECAL_REF", "F:/c5-64bit-work/0166/4104/ref"); }
std::string decalGameDir() { return env("C5R_DECAL_GAME_DIR", "F:/c5-instances/c5h-g/install"); }
std::string decalMods() {
    return env("C5R_DECAL_MODS", "F:/c5-instances/c5h-g/Documents/My Games/Sid Meier's Civilization 5/MODS/(2) Vox Populi");
}
const char* kTiles[4] = {"A_roadfarm", "B_river", "C_run4_f335", "D_run4_f337"};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

struct Sub { uint32_t rowPitch, depthPitch; std::vector<uint8_t> data; };
struct RefTex {
    uint32_t format = 0, w = 0, h = 0, depth = 0, mips = 0, array = 0, flags = 0;
    std::vector<Sub> subs;
};

bool loadC5rt(const std::string& path, RefTex& t) {
    std::vector<uint8_t> b;
    if (!readFile(path, b) || b.size() < 40 || std::memcmp(b.data(), "C5RT", 4) != 0) return false;
    uint32_t h[9];
    std::memcpy(h, b.data() + 4, sizeof h);
    t.format = h[1]; t.w = h[2]; t.h = h[3]; t.depth = h[4]; t.mips = h[5]; t.array = h[6]; t.flags = h[7];
    size_t at = 40;
    t.subs.clear();
    for (uint32_t i = 0; i < h[8]; ++i) {
        uint32_t s[3];
        if (at + 12 > b.size()) return false;
        std::memcpy(s, b.data() + at, 12);
        at += 12;
        if (at + s[2] > b.size()) return false;
        t.subs.push_back(Sub{s[0], s[1], std::vector<uint8_t>(b.begin() + at, b.begin() + at + s[2])});
        at += s[2];
    }
    return true;
}

struct Dev {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool create(bool allowWarp, UINT flags = 0) {   // flags: 0642's SINGLETHREADED arm
        const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return true;
        return allowWarp && SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, &fl, 1, D3D11_SDK_VERSION,
                                                        &dev, nullptr, &ctx));
    }
};

// A texture from a .c5rt file (resource in the captured format), plus a view in viewFmt (0: none).
bool makeTex(Dev& d, const RefTex& r, DXGI_FORMAT viewFmt, ComPtr<ID3D11Resource>& tex, ComPtr<ID3D11ShaderResourceView>& srv) {
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const Sub& s : r.subs) init.push_back(D3D11_SUBRESOURCE_DATA{s.data.data(), s.rowPitch, s.depthPitch});
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = viewFmt;
    HRESULT hr;
    if (r.depth > 1) {
        D3D11_TEXTURE3D_DESC t = {r.w, r.h, r.depth, r.mips, static_cast<DXGI_FORMAT>(r.format), D3D11_USAGE_IMMUTABLE,
                                  D3D11_BIND_SHADER_RESOURCE, 0, 0};
        ComPtr<ID3D11Texture3D> t3;
        hr = d.dev->CreateTexture3D(&t, init.data(), &t3);
        tex = t3;
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        v.Texture3D.MipLevels = r.mips;
    } else {
        D3D11_TEXTURE2D_DESC t = {};
        t.Width = r.w; t.Height = r.h; t.MipLevels = r.mips; t.ArraySize = r.array;
        t.Format = static_cast<DXGI_FORMAT>(r.format); t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_IMMUTABLE;
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE; t.MiscFlags = r.flags & 1 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        ComPtr<ID3D11Texture2D> t2;
        hr = d.dev->CreateTexture2D(&t, init.data(), &t2);
        tex = t2;
        if (r.flags & 1) {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            v.TextureCube.MipLevels = r.mips;
        } else {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = r.mips;
        }
    }
    if (FAILED(hr)) return false;
    return viewFmt == DXGI_FORMAT_UNKNOWN || SUCCEEDED(d.dev->CreateShaderResourceView(tex.Get(), &v, &srv));
}

// Tight bytes of mip m of a 2D RGBA8 or RGBA16F texture.
std::vector<uint8_t> readback(Dev& d, ID3D11Texture2D* t, uint32_t m) {
    D3D11_TEXTURE2D_DESC desc;
    t->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(d.dev->CreateTexture2D(&desc, nullptr, &st))) return {};
    d.ctx->CopyResource(st.Get(), t);
    const uint32_t w = std::max(1u, desc.Width >> m), h = std::max(1u, desc.Height >> m);
    const uint32_t bpp = (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? 8 : 4;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(d.ctx->Map(st.Get(), m, D3D11_MAP_READ, 0, &ms))) return {};
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * bpp);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * w * bpp, static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch, w * bpp);
    d.ctx->Unmap(st.Get(), m);
    return out;
}

std::vector<uint8_t> tight(const RefTex& r, uint32_t m) {
    const uint32_t w = std::max(1u, r.w >> m), h = std::max(1u, r.h >> m);
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * w * 4, r.subs[m].data.data() + static_cast<size_t>(y) * r.subs[m].rowPitch, w * 4);
    return out;
}

// Per-texel comparison of two RGBA8 images: share of equal texels and the largest channel difference per texel.
struct Diff {
    size_t texels = 0, equal = 0;
    int median = 0, p95 = 0, maxAbs = 0;   // over the differing texels
    std::vector<int> d;                    // per texel: largest channel difference
};
Diff compareRgba(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    Diff r;
    if (a.size() != b.size() || a.empty()) { r.maxAbs = 999; return r; }
    r.texels = a.size() / 4;
    r.d.resize(r.texels);
    std::vector<int> diffs;
    for (size_t i = 0; i < r.texels; ++i) {
        int m = 0;
        for (int c = 0; c < 4; ++c) m = std::max(m, std::abs(static_cast<int>(a[i * 4 + c]) - static_cast<int>(b[i * 4 + c])));
        r.d[i] = m;
        if (!m) ++r.equal;
        else diffs.push_back(m);
    }
    if (!diffs.empty()) {
        std::sort(diffs.begin(), diffs.end());
        r.median = diffs[diffs.size() / 2];
        r.p95 = diffs[std::min(diffs.size() - 1, static_cast<size_t>(diffs.size() * 0.95))];
        r.maxAbs = diffs.back();
    }
    return r;
}

void writeDiffBmp(const std::string& path, const std::vector<int>& d, uint32_t w, uint32_t h) {
    const uint32_t row = (w * 3 + 3) & ~3u;
    std::vector<uint8_t> f(54 + static_cast<size_t>(row) * h, 0);
    auto put = [&](size_t at, uint32_t v) { std::memcpy(f.data() + at, &v, 4); };
    f[0] = 'B'; f[1] = 'M';
    put(2, static_cast<uint32_t>(f.size())); put(10, 54); put(14, 40); put(18, w); put(22, h);
    f[26] = 1; f[28] = 24; put(34, static_cast<uint32_t>(row) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t v = static_cast<uint8_t>(std::min(255, 32 * d[static_cast<size_t>(y) * w + x]));
            for (int c = 0; c < 3; ++c) f[54 + static_cast<size_t>(h - 1 - y) * row + x * 3 + c] = v;
        }
    CreateDirectoryA((test::workDir() + "/decal").c_str(), nullptr);
    FILE* o = std::fopen(path.c_str(), "wb");
    if (o) { std::fwrite(f.data(), 1, f.size(), o); std::fclose(o); }
}

float f16(uint16_t h) { return DirectX::PackedVector::XMConvertHalfToFloat(h); }
void reg(const std::vector<uint8_t>& cb, uint32_t r, float out[4]) { std::memcpy(out, cb.data() + r * 16, 16); }

bool openExe(shaders::ShaderSection& sec) {
    const std::string exe = test::exePath();
    if (exe.empty()) {
        test::fail(__FILE__, __LINE__, "C5R_EXE is not set (the EXE's shaders are needed)");
        return false;
    }
    Status st = sec.open(wide(exe));
    if (!st.ok()) {
        test::fail(__FILE__, __LINE__, "cannot open the EXE " + exe + ": " + st.str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- one reference tile (mkref_decal.py's tile.txt)

struct RefDecal { uint32_t pass = 0, first = 0, count = 0; std::string vb, cb, t[3]; };
struct RefTile {
    std::string name, dir;
    int ox = 0, oy = 0;
    uint32_t size = 0, shadowDraws = 0;
    bool second = false;
    std::string blendVb, blendIb, blendCb0, blendCb2, height, own, frame, normcb, grid24, sun, shadowlight, lut, expColour, expSpec;
    std::vector<std::pair<uint32_t, std::pair<std::string, uint32_t>>> blendTex;  // slot -> (file, view format)
    std::vector<RefDecal> decals;
    std::map<std::string, std::pair<std::string, std::string>> tex;              // res -> (file, install name)

    bool load(const std::string& tileName) {
        name = tileName;
        dir = refRoot() + "/" + tileName;
        std::vector<uint8_t> txt;
        if (!readFile(dir + "/tile.txt", txt)) {
            test::fail(__FILE__, __LINE__, "reference tile " + tileName + " missing: " + dir +
                                               "/tile.txt (make it with render/tests/decal/mkref_decal.py from 4109's set; "
                                               "set C5R_DECAL_REF)");
            return false;
        }
        std::istringstream in(std::string(txt.begin(), txt.end()));
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream l(line);
            std::string k;
            l >> k;
            if (k == "origin") l >> ox >> oy;
            else if (k == "size") l >> size;
            else if (k == "second") { int n; l >> n; second = n >= 2; }
            else if (k == "shadowdraws") l >> shadowDraws;
            else if (k == "blend") l >> blendVb >> blendIb >> blendCb0 >> blendCb2;
            else if (k == "blendtex") { uint32_t s, f; std::string file; l >> s >> file >> f; blendTex.push_back({s, {file, f}}); }
            else if (k == "height") l >> height;
            else if (k == "own") l >> own;
            else if (k == "frame") l >> frame;
            else if (k == "normcb") l >> normcb;
            else if (k == "grid24") l >> grid24;
            else if (k == "sun") l >> sun;
            else if (k == "shadowlight") l >> shadowlight;
            else if (k == "lut") l >> lut;
            else if (k == "expected") l >> expColour >> expSpec;
            else if (k == "tex") { std::string res, file, nm; l >> res >> file >> nm; tex[res] = {file, nm}; }
            else if (k == "decal") {
                RefDecal d;
                int i;
                l >> i >> d.pass >> d.first >> d.count >> d.vb >> d.cb >> d.t[0] >> d.t[1] >> d.t[2];
                decals.push_back(d);
            }
        }
        if (!size || decals.empty() || expColour.empty()) {
            test::fail(__FILE__, __LINE__, dir + "/tile.txt is incomplete");
            return false;
        }
        return true;
    }
    std::string path(const std::string& f) const { return dir + "/" + f; }
};

// The DecalSet and list of a reference tile, built from the captured draws: vertex runs out of the captured chunks,
// rectangle and priority from each draw's constants, texture names from the install match. Counts the constant
// registers the pass's closed form gets wrong (must be 0).
struct BuiltSet {
    DecalSet set;
    TileDecalList list;
    std::map<std::string, uint32_t> nameIndex;
    size_t cbMismatch = 0;
};
bool buildSet(const RefTile& t, BuiltSet& b) {
    std::map<std::string, std::vector<uint8_t>> chunks;
    for (size_t i = 0; i < t.decals.size(); ++i) {
        const RefDecal& r = t.decals[i];
        if (!chunks.count(r.vb) && !readFile(t.path(r.vb), chunks[r.vb])) {
            test::fail(__FILE__, __LINE__, "missing " + t.path(r.vb));
            return false;
        }
        const std::vector<uint8_t>& ch = chunks[r.vb];
        std::vector<uint8_t> cb;
        if (!readFile(t.path(r.cb), cb) || cb.size() < kDecalCbFloats * 4) {
            test::fail(__FILE__, __LINE__, "missing " + t.path(r.cb));
            return false;
        }
        if ((static_cast<size_t>(r.first) + r.count) * 8 > ch.size()) {
            test::fail(__FILE__, __LINE__, t.name + ": decal " + std::to_string(i) + " outside its chunk");
            return false;
        }
        Decal d;
        float c[kDecalCbFloats];
        std::memcpy(c, cb.data(), sizeof c);
        for (int k = 0; k < 4; ++k) d.rect[k] = c[20 + k];
        d.priority = c[24];
        d.pass = r.pass;
        d.firstVertex = static_cast<uint32_t>(b.set.vertices.size());
        d.vertexCount = r.count;
        d.listOrder = static_cast<uint32_t>(i);
        for (int k = 0; k < 3; ++k) {
            if (r.t[k] == "default") { d.texture[k] = kNoTexture; continue; }
            auto it = t.tex.find(r.t[k]);
            if (it == t.tex.end()) {
                test::fail(__FILE__, __LINE__, t.name + ": texture " + r.t[k] + " not in tile.txt");
                return false;
            }
            const std::string& nm = it->second.second;
            if (!b.nameIndex.count(nm)) {
                b.nameIndex[nm] = static_cast<uint32_t>(b.set.textures.size());
                b.set.textures.push_back(nm);
            }
            d.texture[k] = b.nameIndex[nm];
        }
        const size_t at = b.set.vertices.size();
        b.set.vertices.resize(at + r.count);
        std::memcpy(b.set.vertices.data() + at, ch.data() + static_cast<size_t>(r.first) * 8, static_cast<size_t>(r.count) * 8);
        // the pass's constants against the captured bits (registers 0-5 and 6.x; 6.yzw are read by neither shader)
        float mine[kDecalCbFloats];
        decalConstants(t.ox, t.oy, d, mine);
        if (std::memcmp(mine, c, 25 * 4) != 0) {
            if (!b.cbMismatch)
                for (int k = 0; k < 25; ++k)
                    if (std::memcmp(&mine[k], &c[k], 4))
                        std::printf("  %s decal %zu: constant %d differs: built %.9g, captured %.9g\n", t.name.c_str(), i, k, mine[k], c[k]);
            ++b.cbMismatch;
        }
        b.set.decals.push_back(d);
        b.list.indices.push_back(static_cast<uint32_t>(i));
    }
    b.list.set = &b.set;
    return true;
}

// What a run gives: the near-camera pair (9 mips, as the EXE's CacheBake + GenerateMips) and the BC1 blocks.
struct RunOut {
    std::vector<std::vector<uint8_t>> colour, spec;   // per mip, tight RGBA8
    std::vector<uint8_t> blocks;                      // t0 then t8, all mips
    DecalCounters counters;
};

enum class TexSource { Captured, GameFs, NoDecals };

// The whole bake of a reference tile on a new device: the captured blend inputs through stage 1's BlendPass, the decal
// pass, bakepost; the CacheBake targets copied into a 9-mip RGBA8 pair and their mips generated (the EXE's near-camera
// cache). Returns false (after a named failure) when an input is missing or a step fails.
bool bakeTile(const RefTile& t, TexSource src, RunOut& out, std::string* note = nullptr) {
    shaders::ShaderSection sec;
    if (!openExe(sec)) return false;
    Dev d;
    if (!d.create(false)) {
        test::fail(__FILE__, __LINE__, "no hardware D3D11 device (the comparison needs the GPU the capture was taken on)");
        return false;
    }
    // 1. The blend (stage 1's code on the captured grid, constants and textures).
    std::vector<uint8_t> vb, ib, cb0, cb2;
    if (!readFile(t.path(t.blendVb), vb) || !readFile(t.path(t.blendIb), ib) || !readFile(t.path(t.blendCb0), cb0) ||
        !readFile(t.path(t.blendCb2), cb2)) {
        test::fail(__FILE__, __LINE__, t.name + ": blend inputs missing in " + t.dir);
        return false;
    }
    std::vector<bake::BlendVertex> v(vb.size() / sizeof(bake::BlendVertex));
    std::memcpy(v.data(), vb.data(), v.size() * sizeof(bake::BlendVertex));
    std::vector<uint16_t> ix(ib.size() / 2);
    std::memcpy(ix.data(), ib.data(), ix.size() * 2);
    std::vector<float> layers(384, 0.0f);
    std::memcpy(layers.data(), cb2.data(), std::min(cb2.size(), size_t(1536)));
    cb0.resize(bake::kOwnBlockBytes);
    std::vector<ComPtr<ID3D11Resource>> keep;
    std::vector<ComPtr<ID3D11ShaderResourceView>> keepV;
    bake::BlendTextures bt;
    for (const auto& s : t.blendTex) {
        RefTex r;
        ComPtr<ID3D11Resource> tx;
        ComPtr<ID3D11ShaderResourceView> sv;
        if (!loadC5rt(t.path(s.second.first), r) || !makeTex(d, r, static_cast<DXGI_FORMAT>(s.second.second), tx, sv)) {
            test::fail(__FILE__, __LINE__, t.name + ": blend texture " + s.second.first);
            return false;
        }
        if (s.first < 24) bt.srv[s.first] = sv.Get();
        keep.push_back(tx);
        keepV.push_back(sv);
    }
    bake::BlendPass blend;
    Status st = blend.init(d.dev.Get(), sec);
    if (st.ok()) st = blend.run(d.ctx.Get(), v, ix, cb0, layers, bt);
    if (!st.ok()) { test::fail(__FILE__, __LINE__, t.name + ": blend: " + st.str()); return false; }

    // 2. The decal set and pass.
    BuiltSet bs;
    if (!buildSet(t, bs)) return false;
    gamefs::GameFiles fs;
    DecalPass pass;
    if (src == TexSource::GameFs) {
        gamefs::GameFiles::Options o;
        o.extraLooseRoots.push_back(wide(decalMods()));
        st = fs.open(wide(decalGameDir()), o);
        if (!st.ok()) { test::fail(__FILE__, __LINE__, "gamefs on " + decalGameDir() + ": " + st.str()); return false; }
    }
    st = pass.init(d.dev.Get(), sec, src == TexSource::GameFs ? &fs : nullptr);
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "decal pass init: " + st.str()); return false; }
    if (src == TexSource::Captured)
        for (const auto& kv : t.tex) {
            RefTex r;
            ComPtr<ID3D11Resource> tx;
            ComPtr<ID3D11ShaderResourceView> sv;
            if (!loadC5rt(t.path(kv.second.first), r) || !makeTex(d, r, DXGI_FORMAT_UNKNOWN, tx, sv)) {
                test::fail(__FILE__, __LINE__, t.name + ": decal texture " + kv.second.first);
                return false;
            }
            ComPtr<ID3D11Texture2D> t2;
            tx.As(&t2);
            st = pass.provideTexture(kv.second.second, t2.Get());
            if (!st.ok()) { test::fail(__FILE__, __LINE__, "provideTexture: " + st.str()); return false; }
        }

    // 3. bakepost with the decal hook.
    RefTex h, sun, sh, lut;
    std::vector<uint8_t> own, frame, ncb, g24;
    if (!loadC5rt(t.path(t.height), h) || !loadC5rt(t.path(t.sun), sun) || !loadC5rt(t.path(t.shadowlight), sh) ||
        !loadC5rt(t.path(t.lut), lut) || !readFile(t.path(t.own), own) || !readFile(t.path(t.frame), frame) ||
        !readFile(t.path(t.normcb), ncb) || !readFile(t.path(t.grid24), g24)) {
        test::fail(__FILE__, __LINE__, t.name + ": post inputs missing in " + t.dir);
        return false;
    }
    ComPtr<ID3D11Resource> hT, sunT, shT, lutT;
    ComPtr<ID3D11ShaderResourceView> hV, sunV, shV, lutV;
    if (!makeTex(d, h, DXGI_FORMAT_A8_UNORM, hT, hV) || !makeTex(d, sun, DXGI_FORMAT_BC1_UNORM_SRGB, sunT, sunV) ||
        !makeTex(d, sh, DXGI_FORMAT_BC1_UNORM_SRGB, shT, shV) || !makeTex(d, lut, DXGI_FORMAT_R8G8B8A8_UNORM, lutT, lutV)) {
        test::fail(__FILE__, __LINE__, t.name + ": post textures not created");
        return false;
    }
    std::vector<float> normals(67 * 67 * 3);
    for (size_t i = 0; i < 67 * 67; ++i) {
        bake::CacheVertex cv;
        std::memcpy(&cv, g24.data() + i * 24, 24);
        for (int c = 0; c < 3; ++c) normals[i * 3 + c] = f16(cv.normal[c]);
    }
    bake::PostInputs in;
    for (int i = 0; i < 3; ++i) in.blend[i] = blend.target(i);
    in.blendMipsReady = true;  // the blend made them; the decals force them to be made again
    in.heightPage = hV.Get();
    in.originX = t.ox;
    in.originY = t.oy;
    in.gridNormals = normals.data();
    in.gridNormalCount = 67 * 67;
    in.sunCube = sunV.Get();
    in.shadowCube = shV.Get();
    in.colorKey = lutV.Get();
    in.sizeClass = t.size;
    in.needSecond = t.second;
    float r[4];
    reg(frame, 21, r); std::memcpy(in.k.halfVector, r, 12);
    reg(frame, 17, r); std::memcpy(in.k.shadowDir, r, 12);
    reg(frame, 16, r); std::memcpy(in.k.hexScale, r, 12);
    reg(frame, 22, r); std::memcpy(in.k.heightTexelScale, r, 8);
    reg(own, 73, r); std::memcpy(in.k.heightOffset, r, 8);
    reg(own, 75, r); in.k.shadowDraws = static_cast<uint32_t>(std::lround(1.0 / r[2]));
    reg(ncb, 0, r); std::memcpy(in.k.normalStep, r, 8);
    reg(ncb, 1, r); in.k.normalStrength = r[0];
    if (in.k.shadowDraws != t.shadowDraws) {
        test::fail(__FILE__, __LINE__, t.name + ": shadow draws from the constants differ from the capture's count");
        return false;
    }
    if (src != TexSource::NoDecals) {
        in.decals = &bs.list;
        in.decalPass = &pass;
    }
    bake::BakePost post;
    st = post.init(d.dev.Get(), sec);
    bake::PostOutputs po;
    if (st.ok()) st = post.run(d.ctx.Get(), in, po);
    if (!st.ok()) {
        test::fail(__FILE__, __LINE__, t.name + ": bake: " + st.str());
        if (note) *note = st.str();
        return false;
    }
    ComPtr<ID3D11Texture2D> t0, t8;
    t0.Attach(po.t0);
    t8.Attach(po.t8);
    out.counters = pass.counters();

    // 4. The near-camera pair: the CacheBake output (mip 0) in a 9-mip RGBA8 texture, mips by GenerateMips.
    for (int k = 0; k < 2; ++k) {
        ID3D11Texture2D* cache = post.target(k ? bake::PostTarget::Cache1 : bake::PostTarget::Cache0);
        D3D11_TEXTURE2D_DESC dd = {};
        dd.Width = dd.Height = t.size;
        dd.MipLevels = 9;
        dd.ArraySize = 1;
        dd.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        dd.SampleDesc.Count = 1;
        dd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        dd.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> near9;
        ComPtr<ID3D11ShaderResourceView> nv;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 9;
        if (FAILED(d.dev->CreateTexture2D(&dd, nullptr, &near9)) || FAILED(d.dev->CreateShaderResourceView(near9.Get(), &sv, &nv))) {
            test::fail(__FILE__, __LINE__, "near-camera pair not created");
            return false;
        }
        d.ctx->CopySubresourceRegion(near9.Get(), 0, 0, 0, 0, cache, 0, nullptr);
        d.ctx->GenerateMips(nv.Get());
        for (uint32_t m = 0; m < 9; ++m) (k ? out.spec : out.colour).push_back(readback(d, near9.Get(), m));
    }
    for (ID3D11Texture2D* bc : {t0.Get(), t8.Get()}) {
        if (!bc) continue;
        D3D11_TEXTURE2D_DESC dd;
        bc->GetDesc(&dd);
        dd.Usage = D3D11_USAGE_STAGING;
        dd.BindFlags = 0;
        dd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stg;
        if (FAILED(d.dev->CreateTexture2D(&dd, nullptr, &stg))) continue;
        d.ctx->CopyResource(stg.Get(), bc);
        for (uint32_t m = 0; m < dd.MipLevels; ++m) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (FAILED(d.ctx->Map(stg.Get(), m, D3D11_MAP_READ, 0, &ms))) continue;
            const uint32_t bw = std::max(1u, (dd.Width >> m) / 4), bh = std::max(1u, (dd.Height >> m) / 4);
            for (uint32_t y = 0; y < bh; ++y) {
                const uint8_t* row = static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch;
                out.blocks.insert(out.blocks.end(), row, row + bw * 8);
            }
            d.ctx->Unmap(stg.Get(), m);
        }
    }
    if (bs.cbMismatch) {
        test::fail(__FILE__, __LINE__, t.name + ": " + std::to_string(bs.cbMismatch) + " decal draws whose constants differ from the capture's");
        return false;
    }
    return true;
}

struct TileFigures {
    double colourEqual = 0, specEqual = 0, colourEqualAll = 0, specEqualAll = 0;
    int colourMax = 0, specMax = 0;
};

// Compares a run with the EXE's pair; prints per-mip figures, writes the mip-0 difference images and a CSV row.
TileFigures compareTile(const RefTile& t, const RunOut& o, const char* arm) {
    TileFigures f;
    RefTex ec, es;
    if (!loadC5rt(t.path(t.expColour), ec) || !loadC5rt(t.path(t.expSpec), es) || ec.mips != 9 || es.mips != 9) {
        test::fail(__FILE__, __LINE__, t.name + ": expected pair missing or not 9 mips in " + t.dir);
        return f;
    }
    const std::string csvPath = test::workDir() + "/decal/decal_ref.csv";
    CreateDirectoryA((test::workDir() + "/decal").c_str(), nullptr);
    FILE* csv = std::fopen(csvPath.c_str(), "ab");
    std::printf("%s [%s], tile (%d, %d), %zu decal draws:\n", t.name.c_str(), arm, t.ox, t.oy, t.decals.size());
    for (int k = 0; k < 2; ++k) {
        const RefTex& e = k ? es : ec;
        const std::vector<std::vector<uint8_t>>& mine = k ? o.spec : o.colour;
        size_t eqAll = 0, nAll = 0;
        int mx = 0;
        for (uint32_t m = 0; m < 9 && m < mine.size(); ++m) {
            Diff dm = compareRgba(mine[m], tight(e, m));
            eqAll += dm.equal;
            nAll += dm.texels;
            mx = std::max(mx, dm.maxAbs);
            std::printf("  %s mip %u: texels equal %.4f%% (%zu of %zu); differing: median %d p95 %d max %d\n", k ? "spec  " : "colour", m,
                        dm.texels ? 100.0 * dm.equal / dm.texels : 0.0, dm.equal, dm.texels, dm.median, dm.p95, dm.maxAbs);
            if (csv)
                std::fprintf(csv, "%s,%s,%s,%u,%zu,%zu,%d,%d,%d\n", t.name.c_str(), arm, k ? "spec" : "colour", m, dm.equal, dm.texels,
                             dm.median, dm.p95, dm.maxAbs);
            if (m == 0) {
                (k ? f.specEqual : f.colourEqual) = dm.texels ? static_cast<double>(dm.equal) / dm.texels : 0;
                if (!dm.d.empty())
                    writeDiffBmp(test::workDir() + "/decal/" + t.name + "_" + arm + (k ? "_spec" : "_colour") + "_mip0_diff_x32.bmp",
                                 dm.d, t.size, t.size);
            }
        }
        (k ? f.specEqualAll : f.colourEqualAll) = nAll ? static_cast<double>(eqAll) / nAll : 0;
        (k ? f.specMax : f.colourMax) = mx;
    }
    if (csv) std::fclose(csv);
    return f;
}

// Synthetic EXE: a PE whose .shader section holds shaders compiled here (no game bytes).
struct FakeExe {
    shaders::ShaderSection sec;
    uint32_t vaVs = 0, vaPs = 0;
    bool build(const char* vsSrc, const char* psSrc) {
        std::vector<uint8_t> vs = test::stripLikeExe(test::compileHlsl(vsSrc, "main", "vs_5_0"));
        std::vector<uint8_t> ps = test::stripLikeExe(test::compileHlsl(psSrc, "main", "ps_5_0"));
        if (vs.empty() || ps.empty()) return false;
        std::vector<uint8_t> data = vs;
        if (data.size() % 8) data.resize(data.size() + 4, 0);
        const uint32_t psOff = static_cast<uint32_t>(data.size());
        data.insert(data.end(), ps.begin(), ps.end());
        std::vector<uint8_t> pe = test::buildPe({{".text", 0x1000, 0x10, std::vector<uint8_t>(0x10, 0xC3)},
                                                 {".shader", 0x2000, static_cast<uint32_t>(data.size()), data}});
        if (!sec.parse(pe).ok()) return false;
        vaVs = 0x400000 + 0x2000;
        vaPs = vaVs + psOff;
        return sec.findByVa(vaVs) && sec.findByVa(vaPs);
    }
    DecalShaderVas vas() const { DecalShaderVas v; v.vs = vaVs; v.ps3 = vaPs; return v; }
};
const char* kFakeVs =
    "cbuffer C : register(b0) { float4 r[7]; };"
    "float4 main(float2 p : POSITION, float2 t : TEXCOORD0) : SV_Position { return float4(p * r[5].xy + t * r[4].xy, r[6].x, 1); }";
const char* kFakePs3 =
    "Texture2D t0 : register(t0); Texture2D t1 : register(t1); Texture2D t2 : register(t2); SamplerState s : register(s0);"
    "struct O { float4 a : SV_Target0; float4 b : SV_Target1; float4 c : SV_Target2; };"
    "O main(float4 p : SV_Position) { O o; o.a = t0.Sample(s, p.xy); o.b = t1.Sample(s, p.xy); o.c = t2.Sample(s, p.xy); return o; }";

ComPtr<ID3D11Texture2D> target(Dev& d, DXGI_FORMAT f, uint32_t size, uint32_t mips, UINT bind) {
    D3D11_TEXTURE2D_DESC t = {};
    t.Width = t.Height = size; t.MipLevels = mips; t.ArraySize = 1; t.Format = f; t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = bind;
    t.MiscFlags = mips > 1 && (bind & D3D11_BIND_RENDER_TARGET) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    ComPtr<ID3D11Texture2D> x;
    d.dev->CreateTexture2D(&t, nullptr, &x);
    if (x && (bind & D3D11_BIND_RENDER_TARGET)) {
        // a recognisable start: 0.25 everywhere (RGBA16F 0x3400, RGBA8 64)
        ComPtr<ID3D11RenderTargetView> rv;
        if (SUCCEEDED(d.dev->CreateRenderTargetView(x.Get(), nullptr, &rv))) {
            const float c[4] = {0.25f, 0.25f, 0.25f, 0.25f};
            d.ctx->ClearRenderTargetView(rv.Get(), c);
        }
    }
    return x;
}

// A synthetic decal set: one quad (two triangles) over its rectangle, uv 0..1.
void quadSet(DecalSet& s, float x0, float y0, float x1, float y1, uint32_t pass, uint32_t tex) {
    using DirectX::PackedVector::XMConvertFloatToHalf;
    const float p[6][2] = {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}};
    Decal d;
    d.rect[0] = x0; d.rect[1] = y0; d.rect[2] = x1; d.rect[3] = y1;
    d.priority = 0.5f;
    d.pass = pass;
    d.firstVertex = static_cast<uint32_t>(s.vertices.size());
    d.vertexCount = 6;
    d.texture[0] = d.texture[1] = d.texture[2] = tex;
    for (const auto& v : p)
        s.vertices.push_back(DecalVertex{XMConvertFloatToHalf(v[0]), XMConvertFloatToHalf(v[1]), XMConvertFloatToHalf(v[0] * 8),
                                         XMConvertFloatToHalf(v[1] * 8)});
    s.decals.push_back(d);
}

}  // namespace

// ---------------------------------------------------------------- no game data

C5R_TEST(view_proj_closed_form) {
    // The six DecalViewProj translations of 4109's captures (4.2, 4.5), bit for bit.
    struct Obs { int ox, oy; uint32_t tx, ty; };
    const Obs obs[] = {{576, 1664, 0xc19368da, 0x424d9565}, {704, 1664, 0xc1b2709c, 0x424d9565}, {512, 1664, 0xc183e4f8, 0x424d9565},
                       {640, 1664, 0xc1a2ecba, 0x424d9565}, {512, 1600, 0xc183e4f8, 0x4245d375}, {576, 1472, 0xc19368da, 0x42364f94}};
    for (const Obs& o : obs) {
        float m[16];
        decalViewProj(o.ox, o.oy, m);
        uint32_t b[16];
        std::memcpy(b, m, sizeof b);
        CHECK_EQ(b[12], o.tx);
        CHECK_EQ(b[13], o.ty);
        CHECK_EQ(b[0], 0x3cf83e10u);
        CHECK_EQ(b[5], 0xbcf83e10u);
        CHECK_EQ(b[10], 0xbf000000u);
        CHECK_EQ(b[14], 0x3f000000u);
        CHECK_EQ(b[15], 0x3f800000u);
        CHECK(b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[6] == 0 && b[7] == 0 && b[8] == 0 && b[9] == 0 && b[11] == 0);
    }
    Decal d;
    d.rect[0] = 1; d.rect[1] = 2; d.rect[2] = 3; d.rect[3] = 4;
    d.priority = 0.7f;
    float c[kDecalCbFloats];
    decalConstants(0, 0, d, c);
    CHECK(c[16] == 1 && c[17] == 1 && c[18] == 0 && c[19] == 0);
    CHECK(c[20] == 1 && c[21] == 2 && c[22] == 3 && c[23] == 4);
    CHECK(c[24] == 0.7f && c[25] == 0 && c[26] == 0 && c[27] == 0);
    uint8_t px[256];
    defaultDecalTexels(px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
    size_t nonzero = 0;
    for (int i = 4; i < 256; ++i) nonzero += px[i] != 0;
    CHECK_EQ(nonzero, size_t(0));
}

C5R_TEST(count_by_pass) {
    DecalSet s;
    quadSet(s, 0, 0, 1, 1, 0, kNoTexture);
    quadSet(s, 0, 0, 1, 1, 1, kNoTexture);
    quadSet(s, 0, 0, 1, 1, 0, kNoTexture);
    TileDecalList l;
    CHECK_EQ(DecalPass::count(l, 0), size_t(0));
    l.set = &s;
    l.indices = {0, 1, 2, 7};
    CHECK_EQ(DecalPass::count(l, 0), size_t(2));
    CHECK_EQ(DecalPass::count(l, 1), size_t(1));
}

C5R_TEST(negative_init_and_order) {
    DecalPass p;
    shaders::ShaderSection empty;
    CHECK_ERR(p.init(nullptr, empty, nullptr), Err::BadLayout);
    Dev d;
    CHECK(d.create(true));
    TileDecalList l;
    ID3D11Texture2D* none[3] = {};
    CHECK_ERR(p.drawBlend(d.ctx.Get(), 0, 0, l, none), Err::NotFound);          // before init
    CHECK_ERR(p.provideTexture("x.dds", nullptr), Err::NotFound);
    FakeExe fx;
    CHECK(fx.build(kFakeVs, kFakePs3));
    CHECK_ERR(p.init(d.dev.Get(), fx.sec, nullptr), Err::NotFound);              // the default VAs are not in the fake EXE
    DecalShaderVas v = fx.vas();
    v.ps3 = 0x00500000;
    CHECK_ERR(p.init(d.dev.Get(), fx.sec, nullptr, v), Err::NotFound);           // one VA missing
    v = fx.vas();
    v.vs = fx.vaPs;
    CHECK_ERR(p.init(d.dev.Get(), fx.sec, nullptr, v), Err::BadLayout);          // a PS where the VS belongs
    v = fx.vas();
    v.ps3 = fx.vaVs;
    CHECK_ERR(p.init(d.dev.Get(), fx.sec, nullptr, v), Err::BadLayout);          // a VS where the PS belongs
    FakeExe two;                                                                 // two targets (pass 0's PS shape)
    CHECK(two.build(kFakeVs, "Texture2D t0 : register(t0); Texture2D t1 : register(t1); Texture2D t2 : register(t2); "
                             "SamplerState s : register(s0); struct O { float4 a : SV_Target0; float4 b : SV_Target1; };"
                             "O main(float4 p : SV_Position) { O o; o.a = t0.Sample(s, p.xy) + t2.Sample(s, p.xy); o.b = t1.Sample(s, p.xy); return o; }"));
    CHECK_ERR(p.init(d.dev.Get(), two.sec, nullptr, two.vas()), Err::BadLayout);
    FakeExe cube;                                                                // t1 a cube
    CHECK(cube.build(kFakeVs, "Texture2D t0 : register(t0); TextureCube t1 : register(t1); Texture2D t2 : register(t2); "
                              "SamplerState s : register(s0); struct O { float4 a : SV_Target0; float4 b : SV_Target1; float4 c : SV_Target2; };"
                              "O main(float4 p : SV_Position) { O o; o.a = t0.Sample(s, p.xy); o.b = t1.Sample(s, p.xyz); o.c = t2.Sample(s, p.xy); return o; }"));
    CHECK_ERR(p.init(d.dev.Get(), cube.sec, nullptr, cube.vas()), Err::BadLayout);
    FakeExe bigcb;                                                               // a VS reading beyond the 7 registers
    CHECK(bigcb.build("cbuffer C : register(b0) { float4 r[9]; };"
                      "float4 main(float2 p : POSITION, float2 t : TEXCOORD0) : SV_Position { return float4(p, t) * r[8]; }",
                      kFakePs3));
    CHECK_ERR(p.init(d.dev.Get(), bigcb.sec, nullptr, bigcb.vas()), Err::BadLayout);
    FakeExe input;                                                               // a VS input the layout does not have
    CHECK(input.build("cbuffer C : register(b0) { float4 r[7]; };"
                      "float4 main(float2 p : POSITION, float4 c : COLOR0) : SV_Position { return float4(p, 0, 1) * r[0] + c; }",
                      kFakePs3));
    CHECK_ERR(p.init(d.dev.Get(), input.sec, nullptr, input.vas()), Err::BadLayout);
    CHECK_OK(p.init(d.dev.Get(), fx.sec, nullptr, fx.vas()));                     // the right shape is taken
    CHECK_ERR(p.provideTexture("x.dds", nullptr), Err::BadLayout);
}

// ---------------------------------------------------------------- 0393: a change of the tie rule (no game data)
//
// The residency bakes a tile with the set in force and bakes it again only when a change raises it. A generation that
// brings the walk block (or loses it) changes the order of equal priorities, and the selection, on tiles whose decals
// did not change. The model below keeps, per tile, the list it was last baked with: every tile baked under the first
// set, then the tiles the change raises (the changed rectangles through tilesForRects, plus tieRuleTiles as the server
// sends them: a point rectangle at each tile's centre) baked again under the second. No tile may then hold a list other
// than decalsForTile of the set in force.

namespace {

// A decal of the tie model: a square of side `side` centred at (cx, cy), priority p, content id `id`.
void tieDecal(DecalSet& s, float cx, float cy, float side, float p, uint32_t pass, uint64_t id) {
    Decal d;
    d.rect[0] = cx - side / 2; d.rect[1] = cy - side / 2; d.rect[2] = cx + side / 2; d.rect[3] = cy + side / 2;
    d.priority = p;
    d.pass = pass;
    d.vertexCount = 6;
    d.listOrder = static_cast<uint32_t>(s.decals.size());
    d.hash = 0x9E3779B97F4A7C15ull * (id + 1);
    s.decals.push_back(d);
}

// The EXE's index for the set's decals as if each was added in record order (terrain-decals.md 18.1): a small decal in
// the cell of its rectangle's centre, at the head of the cell's list (rank 0 = the newest add); a large one appended to
// the large list. `cell` samples per cell, the grid from the world's minimum.
void addWalk(DecalSet& s, float cell) {
    DecalWalk& w = s.walk;
    w = DecalWalk();
    w.origin[0] = s.worldRect[0];
    w.origin[1] = s.worldRect[1];
    w.inverse[0] = w.inverse[1] = 1.0f / cell;
    w.cols = static_cast<uint32_t>(std::floor((s.worldRect[2] - s.worldRect[0]) / cell)) + 1;
    w.rows = static_cast<uint32_t>(std::floor((s.worldRect[3] - s.worldRect[1]) / cell)) + 1;
    const size_t n = s.decals.size();
    w.cell.assign(n, 0);
    w.rank.assign(n, 0);
    w.point.assign(2 * n, 0.0f);
    std::vector<uint32_t> inCell(static_cast<size_t>(w.cols) * w.rows, 0);
    uint32_t large = 0;
    for (size_t k = 0; k < n; ++k) {
        const float* r = s.decals[k].rect;
        const float px = (r[0] + r[2]) * 0.5f, py = (r[1] + r[3]) * 0.5f;
        w.point[2 * k] = px;
        w.point[2 * k + 1] = py;
        if (r[2] - r[0] > kLargeDecal || r[3] - r[1] > kLargeDecal) {
            w.cell[k] = kWalkLarge;
            w.rank[k] = large++;
            continue;
        }
        const int cx = (std::min)(static_cast<int>(w.cols) - 1, (std::max)(0, static_cast<int>((px - w.origin[0]) * w.inverse[0])));
        const int cy = (std::min)(static_cast<int>(w.rows) - 1, (std::max)(0, static_cast<int>((py - w.origin[1]) * w.inverse[1])));
        w.cell[k] = static_cast<uint32_t>(cy) * w.cols + static_cast<uint32_t>(cx);
        inCell[w.cell[k]]++;
    }
    // newest add first: rank = the number of later adds into the same cell
    std::vector<uint32_t> seen(inCell.size(), 0);
    for (size_t k = n; k-- > 0;)
        if (w.cell[k] != kWalkLarge) w.rank[k] = seen[w.cell[k]]++;
}

struct TieModel {
    const DecalSet* inForce = nullptr;
    std::map<std::pair<int, int>, std::vector<uint64_t>> baked;   // tile origin -> the content ids it was baked with
    int64_t rebakes = 0;

    // what the decal pass draws: pass 0's decals in list order (the blend's targets), then pass 1's (the macro shadow)
    static std::vector<uint64_t> list(const DecalSet& s, int ox, int oy) {
        std::vector<uint64_t> out;
        const TileDecalList l = decalsForTile(s, ox, oy);
        for (uint32_t pass = 0; pass < 2; ++pass) {
            for (uint32_t i : l.indices)
                if (s.decals[i].pass == pass) out.push_back(s.decals[i].hash);
            out.push_back(0);   // the end of a pass (no decal hash is 0 here)
        }
        return out;
    }
    std::vector<std::pair<int, int>> worldTiles() const {
        std::vector<std::pair<int, int>> t;
        const float* w = inForce->worldRect;
        for (int y = static_cast<int>(std::floor(w[1] / kTileSamples)); y <= static_cast<int>(std::floor(w[3] / kTileSamples)); ++y)
            for (int x = static_cast<int>(std::floor(w[0] / kTileSamples)); x <= static_cast<int>(std::floor(w[2] / kTileSamples)); ++x)
                t.push_back(std::make_pair(x * kTileSamples, y * kTileSamples));
        return t;
    }
    void bakeAll(const DecalSet& s) {
        inForce = &s;
        for (const auto& t : worldTiles()) baked[t] = list(s, t.first, t.second);
    }
    // the residency's side of a change: the changed rectangles plus the server's point rectangles of the tie tiles
    void change(const DecalSet& s, std::vector<DecalRect> rects, bool withTieRule, size_t* tieTiles = nullptr) {
        const DecalSet& prev = *inForce;
        if (withTieRule) {
            const std::vector<TileOrigin> tie = tieRuleTiles(prev, s);
            if (tieTiles) *tieTiles = tie.size();
            for (const TileOrigin& t : tie) {
                DecalRect r;
                r.v[0] = r.v[2] = static_cast<float>(t.x + kTileSamples / 2);
                r.v[1] = r.v[3] = static_cast<float>(t.y + kTileSamples / 2);
                rects.push_back(r);
            }
        }
        inForce = &s;
        for (const TileOrigin& t : tilesForRects(rects.data(), rects.size(), s.worldRect)) {
            baked[std::make_pair(t.x, t.y)] = list(s, t.x, t.y);
            rebakes++;
        }
    }
    size_t stale() const {
        size_t n = 0;
        for (const auto& t : worldTiles())
            if (baked.at(t) != list(*inForce, t.first, t.second)) n++;
        return n;
    }
};

std::vector<std::pair<int, int>> tilePairs(const std::vector<TileOrigin>& v) {
    std::vector<std::pair<int, int>> out;
    for (const TileOrigin& t : v) out.push_back(std::make_pair(t.x, t.y));
    return out;
}

}  // namespace

C5R_TEST(tie_rule_change_leaves_no_stale_tile) {
    // A world of 4 x 4 tiles (256 samples); the EXE's index one cell per tile (64 samples).
    DecalSet a;
    a.generation = 1;
    a.worldRect[0] = 0; a.worldRect[1] = 0; a.worldRect[2] = 255; a.worldRect[3] = 255;
    // Tile (0, 0): two ties in different cells. X (added first) centred in cell (1, 0), Y (added later) in cell (0, 1); both
    // reach the tile. The 4111 rule: newest first, Y then X. The walk: rows outer, cell (1, 0) before (0, 1): X then Y.
    tieDecal(a, 70, 10, 20, 0.5f, 0, 10);    // X: 60..80 x 0..20
    tieDecal(a, 10, 70, 20, 0.5f, 0, 11);    // Y: 0..20 x 60..80
    // Tile (64, 192): Z ends at x 62.5, inside the 4111 square [62, 130] but not the EXE's tile rectangle [63, 129].
    tieDecal(a, 52, 200, 21, 0.5f, 0, 12);
    // Tile (192, 192): a lone decal (and the large one): the same list under both rules.
    tieDecal(a, 220, 220, 16, 0.3f, 0, 20);
    // Tile (128, 0): two decals of different priority: the same order under both rules.
    tieDecal(a, 150, 30, 12, 0.2f, 0, 30);
    tieDecal(a, 160, 40, 12, 0.7f, 0, 31);
    // A large decal (a side over 256 samples) across the world, priority 0.5: the large list after the small decals.
    tieDecal(a, 128, 128, 300, 0.5f, 0, 40);
    // A pass-1 decal (tree shadow).
    tieDecal(a, 100, 200, 8, 0.5f, 1, 50);
    // Tile (192, 0): a pass-0 and a pass-1 decal of equal priority. The 4111 rule puts the newer (pass 1) first, the walk
    // pass 0 first: the merged lists differ, each pass's list is the same, so nothing to re-bake.
    tieDecal(a, 220, 30, 10, 0.5f, 0, 70);
    tieDecal(a, 230, 40, 10, 0.5f, 1, 71);
    buildIndex(a);
    DecalSet b = a;                      // the same decals, now with the walk block
    b.generation = 2;
    addWalk(b, 64.0f);
    buildIndex(b);
    CHECK(!hasWalk(a));
    CHECK(hasWalk(b));

    // The model's premise: the two rules disagree on at least one tile, and agree on the lone and the ordered ones.
    TieModel probe;
    probe.bakeAll(a);
    CHECK(TieModel::list(a, 0, 0) != TieModel::list(b, 0, 0));
    CHECK(TieModel::list(a, 64, 192) != TieModel::list(b, 64, 192));
    CHECK(decalsForTile(a, 0, 0).indices.size() == 3 && decalsForTile(b, 0, 0).indices.size() == 3);   // X, Y, the large one
    CHECK(TieModel::list(a, 192, 192) == TieModel::list(b, 192, 192));
    CHECK(TieModel::list(a, 128, 0) == TieModel::list(b, 128, 0));
    CHECK(TieModel::list(a, 192, 0) == TieModel::list(b, 192, 0));
    CHECK(decalsForTile(a, 192, 0).indices != decalsForTile(b, 192, 0).indices);

    // Teeth: the change without the tie rule (the server before 0393: no decal changed, so no rectangle) leaves stale tiles.
    {
        TieModel m;
        m.bakeAll(a);
        m.change(b, {}, false);
        CHECK(m.stale() > 0);
        CHECK_EQ(m.rebakes, int64_t(0));
    }
    // A generation without the block, then with it: no stale tile; only the tiles whose list changed are re-baked.
    std::vector<TileOrigin> tie = tieRuleTiles(a, b);
    {
        TieModel m;
        m.bakeAll(a);
        size_t n = 0;
        m.change(b, {}, true, &n);
        CHECK_EQ(m.stale(), size_t(0));
        CHECK_EQ(static_cast<size_t>(m.rebakes), n);
        CHECK(n > 0 && n < m.worldTiles().size());
    }
    // exactly the tiles whose list differs, in tilesForRects' order (y then x)
    {
        std::vector<std::pair<int, int>> want;
        for (const auto& t : probe.worldTiles())
            if (TieModel::list(a, t.first, t.second) != TieModel::list(b, t.first, t.second)) want.push_back(t);
        CHECK(tilePairs(tie) == want);
        bool hasOrigin = false, hasPassTile = false;
        for (const auto& t : want) {
            hasOrigin = hasOrigin || (t.first == 0 && t.second == 0);
            hasPassTile = hasPassTile || (t.first == 192 && t.second == 0);
        }
        CHECK(hasOrigin);
        CHECK(!hasPassTile);
    }
    // The server's point rectangle at a tile's centre names that tile alone.
    for (const TileOrigin& t : tie) {
        DecalRect r;
        r.v[0] = r.v[2] = static_cast<float>(t.x + kTileSamples / 2);
        r.v[1] = r.v[3] = static_cast<float>(t.y + kTileSamples / 2);
        const std::vector<TileOrigin> one = tilesForRects(&r, 1, b.worldRect);
        CHECK(one.size() == 1 && one[0].x == t.x && one[0].y == t.y);
    }
    // ... and back: the block goes again (a walk miss in the tap): no stale tile.
    DecalSet c = a;
    c.generation = 3;
    {
        TieModel m;
        m.bakeAll(a);
        m.change(b, {}, true);
        m.change(c, {}, true);
        CHECK_EQ(m.stale(), size_t(0));
        CHECK(tilePairs(tieRuleTiles(b, c)) == tilePairs(tie));
    }
    // The same rule on both sides: nothing (the changed rectangles cover content changes).
    CHECK(tieRuleTiles(a, c).empty());
    CHECK(tieRuleTiles(b, b).empty());
    TieRuleStats st;
    st.candidates = 99;
    CHECK(tieRuleTiles(a, a, &st).empty());
    CHECK_EQ(st.candidates, 0u);
    // A content change together with the rule change: a decal added (its rectangle listed) in the generation that brings
    // the block. Still no stale tile.
    {
        DecalSet d = a;
        tieDecal(d, 180, 180, 40, 0.5f, 0, 60);
        d.generation = 4;
        addWalk(d, 64.0f);
        buildIndex(d);
        DecalRect added;
        std::memcpy(added.v, d.decals.back().rect, sizeof(added.v));
        TieModel m;
        m.bakeAll(a);
        m.change(d, {added}, true);
        CHECK_EQ(m.stale(), size_t(0));
        TieModel teeth;   // the added rectangle alone does not cover the tie tiles
        teeth.bakeAll(a);
        teeth.change(d, {added}, false);
        CHECK(teeth.stale() > 0);
    }
    // Stats: every tile with a decal in either grid compared, the differing ones counted.
    tieRuleTiles(a, b, &st);
    CHECK_EQ(st.differ, static_cast<uint32_t>(tie.size()));
    CHECK(st.candidates >= st.differ && st.candidates <= 16u);
    CHECK(st.decalsCompared > 0);
    // A world rectangle that is not finite or inverted: nothing (never a crash).
    DecalSet bad = b;
    bad.worldRect[2] = std::nanf("");
    CHECK(tieRuleTiles(a, bad).empty());
    bad.worldRect[2] = -10;
    CHECK(tieRuleTiles(a, bad).empty());
}

// Random sets: ties of a few priorities, some large decals, some pass 1, an index of random cell size. Without the walk,
// then with it, then without again, with random adds and removes listed as changed rectangles in each step: no tile is
// ever stale, and without the tie rule the first step leaves some stale tile in most sets.
C5R_TEST(tie_rule_change_random_sets) {
    uint64_t rng = 0x0393ull;
    auto next = [&]() {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return rng;
    };
    auto uni = [&](float lo, float hi) { return lo + (hi - lo) * static_cast<float>(next() % 1000000) / 1000000.0f; };
    int setsWithStaleBefore = 0;
    const int kSets = 24;
    for (int iter = 0; iter < kSets; ++iter) {
        const int tilesX = 3 + static_cast<int>(next() % 6), tilesY = 2 + static_cast<int>(next() % 5);
        DecalSet a;
        a.generation = 1;
        a.worldRect[0] = 0; a.worldRect[1] = 0;
        a.worldRect[2] = static_cast<float>(tilesX * kTileSamples - 1);
        a.worldRect[3] = static_cast<float>(tilesY * kTileSamples - 1);
        const int n = 20 + static_cast<int>(next() % 200);
        uint64_t id = 1000;
        for (int k = 0; k < n; ++k) {
            const bool large = next() % 20 == 0;
            const float side = large ? uni(260, 400) : uni(2, 120);
            tieDecal(a, uni(0, a.worldRect[2]), uni(0, a.worldRect[3]), side, 0.1f * static_cast<float>(next() % 3),
                     next() % 7 == 0 ? 1u : 0u, id++);
        }
        const float cell = 64.0f + static_cast<float>(next() % 4) * 64.0f;
        buildIndex(a);
        // b: a's decals minus a few, plus a few, with the walk
        DecalSet b;
        b.generation = 2;
        std::memcpy(b.worldRect, a.worldRect, sizeof(b.worldRect));
        std::vector<DecalRect> changed;
        for (const Decal& d : a.decals) {
            if (next() % 25 == 0) {
                DecalRect r;
                std::memcpy(r.v, d.rect, sizeof(r.v));
                changed.push_back(r);
                continue;
            }
            Decal k = d;
            k.listOrder = static_cast<uint32_t>(b.decals.size());
            b.decals.push_back(k);
        }
        for (int k = 0; k < 3; ++k) {
            tieDecal(b, uni(0, b.worldRect[2]), uni(0, b.worldRect[3]), uni(2, 100), 0.1f * static_cast<float>(next() % 3), 0, id++);
            DecalRect r;
            std::memcpy(r.v, b.decals.back().rect, sizeof(r.v));
            changed.push_back(r);
        }
        addWalk(b, cell);
        buildIndex(b);
        CHECK(hasWalk(b));
        DecalSet c = b;   // the block goes again; nothing else changes
        c.generation = 3;
        c.walk = DecalWalk();
        buildIndex(c);
        {
            TieModel m;
            m.bakeAll(a);
            m.change(b, changed, true);
            CHECK_EQ(m.stale(), size_t(0));
            m.change(c, {}, true);
            CHECK_EQ(m.stale(), size_t(0));
        }
        {
            TieModel m;
            m.bakeAll(a);
            m.change(b, changed, false);
            if (m.stale() > 0) setsWithStaleBefore++;
        }
    }
    std::printf("  tie rule: %d of %d random sets had a stale tile without the rule\n", setsWithStaleBefore, kSets);
    CHECK(setsWithStaleBefore >= kSets / 2);
}

// ---------------------------------------------------------------- the EXE's shaders (C5R_EXE), synthetic decals

namespace {
struct Fixture {
    shaders::ShaderSection sec;
    Dev d;
    DecalPass pass;
    ComPtr<ID3D11Texture2D> blend[3], shadow;
    bool init(gamefs::GameFiles* fs = nullptr, UINT deviceFlags = 0) {
        if (!openExe(sec)) return false;
        if (!d.create(true, deviceFlags)) { test::fail(__FILE__, __LINE__, "no D3D11 device"); return false; }
        Status st = pass.init(d.dev.Get(), sec, fs);
        if (!st.ok()) { test::fail(__FILE__, __LINE__, "init: " + st.str()); return false; }
        const UINT rtsr = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        for (int i = 0; i < 3; ++i) blend[i] = target(d, DXGI_FORMAT_R16G16B16A16_FLOAT, 512, 10, rtsr);
        shadow = target(d, DXGI_FORMAT_R8G8B8A8_TYPELESS, 512, 10, rtsr);
        return blend[0] && blend[1] && blend[2] && shadow;
    }
    ID3D11Texture2D* b[3];
    ID3D11Texture2D* const* targets() { for (int i = 0; i < 3; ++i) b[i] = blend[i].Get(); return b; }
    std::vector<uint8_t> snapshot() {
        std::vector<uint8_t> all;
        for (int i = 0; i < 3; ++i) {
            std::vector<uint8_t> m = readback(d, blend[i].Get(), 0);
            all.insert(all.end(), m.begin(), m.end());
        }
        std::vector<uint8_t> s = readback(d, shadow.Get(), 0);
        all.insert(all.end(), s.begin(), s.end());
        return all;
    }
};
}  // namespace

C5R_TEST(game_synthetic_decals_and_negatives) {
    Fixture f;
    if (!f.init()) return;
    ID3D11DeviceContext* ctx = f.d.ctx.Get();
    const std::vector<uint8_t> before = f.snapshot();
    DecalSet s;
    quadSet(s, 0, 0, 64, 64, 0, kNoTexture);          // 0: over the tile (0, 0)'s square
    quadSet(s, 5000, 5000, 5100, 5100, 0, kNoTexture);  // 1: far off the tile
    quadSet(s, 0, 0, 64, 64, 1, kNoTexture);          // 2: a macro-shadow decal
    TileDecalList l;
    l.set = &s;

    // an empty list and a list of only off-tile decals draw nothing
    CHECK_OK(f.pass.drawBlend(ctx, 0, 0, l, f.targets()));
    CHECK_OK(f.pass.drawMacroShadow(ctx, 0, 0, l, nullptr));
    l.indices = {1};
    CHECK_OK(f.pass.drawBlend(ctx, 0, 0, l, f.targets()));
    CHECK(f.snapshot() == before);
    CHECK_EQ(f.pass.counters().draws, uint64_t(1));
    // no pass-1 decal in the list: drawMacroShadow does nothing, a null target is fine
    CHECK_OK(f.pass.drawMacroShadow(ctx, 0, 0, l, nullptr));

    // negatives: each fails before drawing anything
    auto expectFail = [&](const TileDecalList& bad, Err want, const char* what) {
        Status st = f.pass.drawBlend(ctx, 0, 0, bad, f.targets());
        if (st.code != want) test::fail(__FILE__, __LINE__, std::string(what) + ": want " + errName(want) + ", got " + st.str());
    };
    TileDecalList bad;
    bad.indices = {0};
    expectFail(bad, Err::BadLayout, "list without a set");
    bad.set = &s;
    bad.indices = {0, 9};
    expectFail(bad, Err::BadLayout, "list index outside the set");
    DecalSet s2 = s;
    s2.decals[0].texture[0] = 5;                       // the set has no names
    bad.set = &s2;
    bad.indices = {0};
    expectFail(bad, Err::BadLayout, "texture index outside the set");
    s2 = s;
    s2.decals[0].vertexCount = 18;                     // past the set's 18 vertices from first vertex 0? 0 + 18 = 18: fits
    s2.decals[0].firstVertex = 6;                      // 6 + 18 > 18
    expectFail(bad, Err::BadLayout, "vertex range outside the set");
    s2 = s;
    s2.decals[0].vertexCount = 4;
    expectFail(bad, Err::BadLayout, "vertex count not whole triangles");
    s2.decals[0].vertexCount = 0;
    expectFail(bad, Err::BadLayout, "empty vertex run");
    const uint64_t badBefore = f.pass.counters().badDecals;
    CHECK(badBefore >= 5);
    CHECK(f.snapshot() == before);                     // nothing drawn by any failed call
    s2 = s;
    s2.textures = {"no_such_decal_texture.dds"};
    s2.decals[0].texture[0] = 0;
    expectFail(bad, Err::NotFound, "texture with no game files and not provided");
    CHECK_EQ(f.pass.counters().missingTextures, uint64_t(1));
    // wrong targets
    l.indices = {0};
    ComPtr<ID3D11Texture2D> wrongFmt = target(f.d, DXGI_FORMAT_R8G8B8A8_UNORM, 512, 1, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11Texture2D> wrongSize = target(f.d, DXGI_FORMAT_R16G16B16A16_FLOAT, 256, 1, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11Texture2D> noRt = target(f.d, DXGI_FORMAT_R16G16B16A16_FLOAT, 512, 1, D3D11_BIND_SHADER_RESOURCE);
    for (ID3D11Texture2D* w : {wrongFmt.Get(), wrongSize.Get(), noRt.Get(), static_cast<ID3D11Texture2D*>(nullptr)}) {
        ID3D11Texture2D* t3[3] = {f.blend[0].Get(), w, f.blend[2].Get()};
        CHECK_ERR(f.pass.drawBlend(ctx, 0, 0, l, t3), Err::BadLayout);
    }
    l.indices = {2};
    CHECK_ERR(f.pass.drawMacroShadow(ctx, 0, 0, l, nullptr), Err::BadLayout);
    CHECK_ERR(f.pass.drawMacroShadow(ctx, 0, 0, l, f.blend[0].Get()), Err::BadLayout);  // RGBA16F is not the shadow format
    CHECK_ERR(f.pass.drawBlend(nullptr, 0, 0, l, f.targets()), Err::BadLayout);
    CHECK(f.snapshot() == before);

    // a decal over the tile changes the blend targets (only pass 0 draws there), and the shadow decal only the shadow
    l.indices = {0, 2};
    CHECK_OK(f.pass.drawBlend(ctx, 0, 0, l, f.targets()));
    std::vector<uint8_t> afterBlend = f.snapshot();
    const size_t blendBytes = 3 * 512 * 512 * 8;
    CHECK(std::memcmp(afterBlend.data(), before.data(), blendBytes) != 0);
    CHECK(std::memcmp(afterBlend.data() + blendBytes, before.data() + blendBytes, 512 * 512 * 4) == 0);
    CHECK_OK(f.pass.drawMacroShadow(ctx, 0, 0, l, f.shadow.Get()));
    std::vector<uint8_t> afterShadow = f.snapshot();
    CHECK(std::memcmp(afterShadow.data(), afterBlend.data(), blendBytes) == 0);
    CHECK(std::memcmp(afterShadow.data() + blendBytes, afterBlend.data() + blendBytes, 512 * 512 * 4) != 0);
    CHECK_EQ(f.pass.counters().tilesWithDecals, uint64_t(2));
}

C5R_TEST(game_missing_texture_file_in_the_install) {
    // an install without the file (empty Assets/ and Resource/): NotFound, counted, nothing drawn
    const std::string root = test::workDir() + "/decal_empty_install";
    CreateDirectoryA(test::workDir().c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "/Assets").c_str(), nullptr);
    CreateDirectoryA((root + "/Resource").c_str(), nullptr);
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(root)));
    Fixture f;
    if (!f.init(&fs)) return;
    const std::vector<uint8_t> before = f.snapshot();
    DecalSet s;
    s.textures = {"Assets/Decals/no_such_decal_texture.dds"};
    quadSet(s, 0, 0, 64, 64, 0, 0);
    TileDecalList l;
    l.set = &s;
    l.indices = {0};
    CHECK_ERR(f.pass.drawBlend(f.d.ctx.Get(), 0, 0, l, f.targets()), Err::NotFound);
    CHECK_EQ(f.pass.counters().missingTextures, uint64_t(1));
    CHECK_EQ(f.pass.counters().draws, uint64_t(0));
    CHECK(f.snapshot() == before);
}

namespace {
// A legacy A8R8G8B8 DDS (DDPF_RGB | DDPF_ALPHAPIXELS, 32 bits, masks R 0xFF0000, G 0xFF00, B 0xFF, A 0xFF000000), one
// mip, every texel the memory bytes (b, g, r, a): the stone quarry's height map's format (terrain-decals.md 14.3).
std::vector<uint8_t> a8r8g8b8Dds(uint32_t size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> f(128 + size * size * 4, 0);
    auto put = [&f](size_t o, uint32_t v) { std::memcpy(f.data() + o, &v, 4); };
    std::memcpy(f.data(), "DDS ", 4);
    put(4, 124);                     // header size
    put(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8);  // CAPS | HEIGHT | WIDTH | PIXELFORMAT | PITCH
    put(12, size);
    put(16, size);
    put(20, size * 4);
    put(28, 1);                      // mips
    put(76, 32);                     // pixel format size
    put(80, 0x40 | 0x1);             // DDPF_RGB | DDPF_ALPHAPIXELS
    put(88, 32);
    put(92, 0x00FF0000u);
    put(96, 0x0000FF00u);
    put(100, 0x000000FFu);
    put(104, 0xFF000000u);
    put(108, 0x1000);                // DDSCAPS_TEXTURE
    for (uint32_t i = 0; i < size * size; ++i) {
        uint8_t* t = f.data() + 128 + 4 * i;
        t[0] = b; t[1] = g; t[2] = r; t[3] = a;
    }
    return f;
}

// An R8G8B8A8_TYPELESS texture of one colour (bytes r, g, b, a in memory), as the capture shows the EXE's legacy files.
ComPtr<ID3D11Texture2D> rgbaTexture(Dev& d, uint32_t size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> px(size * size * 4);
    for (uint32_t i = 0; i < size * size; ++i) { px[4 * i] = r; px[4 * i + 1] = g; px[4 * i + 2] = b; px[4 * i + 3] = a; }
    D3D11_TEXTURE2D_DESC t = {};
    t.Width = t.Height = size; t.MipLevels = t.ArraySize = 1; t.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_IMMUTABLE; t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA s = {px.data(), size * 4, 0};
    ComPtr<ID3D11Texture2D> x;
    d.dev->CreateTexture2D(&t, &s, &x);
    return x;
}

// One covering pass-0 decal over tile (0, 0) whose three textures are `name`; the snapshot of the targets after it.
bool drawOneDecal(Fixture& f, const std::string& name, std::vector<uint8_t>& out) {
    DecalSet s;
    s.textures = {name};
    quadSet(s, 0, 0, 64, 64, 0, 0);
    TileDecalList l;
    l.set = &s;
    l.indices = {0};
    Status st = f.pass.drawBlend(f.d.ctx.Get(), 0, 0, l, f.targets());
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "drawBlend " + name + ": " + st.str()); return false; }
    out = f.snapshot();
    return true;
}
}  // namespace

C5R_TEST(game_a8r8g8b8_decal_texture_has_its_channels_in_their_meaning) {
    // 4113: a legacy A8R8G8B8 file loaded by name through gamefs draws exactly what the same colour given as R8G8B8A8
    // in channel order draws (the EXE samples the stone quarry's red relief as red, 14.3), and not what the file's bytes
    // read as R8G8B8A8 draw (4104's old rule, red and blue swapped). Red, green, blue and alpha all differ.
    const uint8_t R = 220, G = 120, B = 40, A = 200;
    const std::string root = test::workDir() + "/decal_a8r8g8b8_install";
    CreateDirectoryA(test::workDir().c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "/Assets").c_str(), nullptr);
    CreateDirectoryA((root + "/Resource").c_str(), nullptr);
    const std::vector<uint8_t> dds = a8r8g8b8Dds(16, R, G, B, A);
    FILE* fp = std::fopen((root + "/Assets/chan_a8r8g8b8.dds").c_str(), "wb");
    CHECK(fp != nullptr);
    if (!fp) return;
    CHECK_EQ(std::fwrite(dds.data(), 1, dds.size(), fp), dds.size());
    std::fclose(fp);
    gamefs::DdsImage img;
    CHECK_OK(gamefs::parseDds(dds.data(), dds.size(), img));
    CHECK(img.sourceFormat == "A8R8G8B8");
    CHECK(img.format == DXGI_FORMAT_B8G8R8A8_UNORM);
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(root)));

    std::vector<uint8_t> byName, inMeaning, swapped, before;
    {
        Fixture f;
        if (!f.init(&fs)) return;
        before = f.snapshot();
        if (!drawOneDecal(f, "Assets/chan_a8r8g8b8.dds", byName)) return;
        CHECK_EQ(f.pass.counters().texturesLoaded, uint64_t(1));
    }
    {
        Fixture f;
        if (!f.init()) return;
        ComPtr<ID3D11Texture2D> t = rgbaTexture(f.d, 16, R, G, B, A);
        CHECK_OK(f.pass.provideTexture("chan_meaning.dds", t.Get()));
        if (!drawOneDecal(f, "chan_meaning.dds", inMeaning)) return;
    }
    {
        Fixture f;
        if (!f.init()) return;
        ComPtr<ID3D11Texture2D> t = rgbaTexture(f.d, 16, B, G, R, A);  // the file's bytes as R8G8B8A8
        CHECK_OK(f.pass.provideTexture("chan_swapped.dds", t.Get()));
        if (!drawOneDecal(f, "chan_swapped.dds", swapped)) return;
    }
    CHECK(byName != before);          // the decal drew
    CHECK(byName == inMeaning);       // channels in their meaning, bit for bit
    CHECK(byName != swapped);         // the negative: the old reading gives another picture
    CHECK(inMeaning != swapped);
}

// ---------------------------------------------------------------- the four reference tiles (C5R_DECAL_REF)

namespace {
// Runs one tile with the captured textures (twice, on two devices) and with no decals; the acceptance is bit for bit.
void refTile(int i) {
    RefTile t;
    if (!t.load(kTiles[i])) return;
    RunOut a, b, none;
    if (!bakeTile(t, TexSource::Captured, a)) return;
    TileFigures f = compareTile(t, a, "captured_textures");
    std::printf("  draws %llu, textures loaded from files %llu\n", static_cast<unsigned long long>(a.counters.draws),
                static_cast<unsigned long long>(a.counters.texturesLoaded));
    CHECK_EQ(a.counters.draws, static_cast<uint64_t>(t.decals.size()));
    if (!bakeTile(t, TexSource::Captured, b)) return;
    CHECK(a.colour == b.colour && a.spec == b.spec && a.blocks == b.blocks);
    std::printf("  second run on a new device: %s\n", a.colour == b.colour && a.spec == b.spec && a.blocks == b.blocks ? "identical" : "DIFFERENT");
    if (bakeTile(t, TexSource::NoDecals, none)) compareTile(t, none, "no_decals");
    CHECK(f.colourEqualAll == 1.0);
    CHECK(f.specEqualAll == 1.0);
}
}  // namespace

C5R_TEST(game_ref_A_roadfarm) { refTile(0); }
C5R_TEST(game_ref_B_river) { refTile(1); }
C5R_TEST(game_ref_C_run4_f335) { refTile(2); }
C5R_TEST(game_ref_D_run4_f337) { refTile(3); }

// The same tiles with every decal texture loaded by name through c5r::gamefs from the capture's install (packs, VT's
// loose DLC files, the VP mod). First each name's file is compared with the captured texture, subresource by subresource.
C5R_TEST(game_ref_textures_from_the_install) {
    gamefs::GameFiles fs;
    gamefs::GameFiles::Options o;
    o.extraLooseRoots.push_back(wide(decalMods()));
    Status st = fs.open(wide(decalGameDir()), o);
    if (!st.ok()) {
        test::fail(__FILE__, __LINE__, "C5R_DECAL_GAME_DIR " + decalGameDir() + ": " + st.str());
        return;
    }
    std::map<std::string, std::string> checked;
    for (int i = 0; i < 4; ++i) {
        RefTile t;
        if (!t.load(kTiles[i])) continue;
        for (const auto& kv : t.tex) {
            const std::string& nm = kv.second.second;
            if (checked.count(nm)) continue;
            std::vector<uint8_t> bytes;
            std::string origin;
            Status rs = fs.read(nm, bytes, &origin);
            if (!rs.ok()) {
                checked[nm] = "NOT RESOLVED: " + rs.str();
                test::fail(__FILE__, __LINE__, "gamefs cannot resolve decal texture " + nm + ": " + rs.str());
                continue;
            }
            gamefs::DdsImage img;
            RefTex r;
            bool same = gamefs::parseDds(bytes.data(), bytes.size(), img).ok() && loadC5rt(t.path(kv.second.first), r) &&
                        img.subresources.size() == r.subs.size();
            for (size_t k = 0; same && k < r.subs.size(); ++k)
                same = img.subresources[k].size == r.subs[k].data.size() &&
                       std::memcmp(img.pixels() + img.subresources[k].offset, r.subs[k].data.data(), r.subs[k].data.size()) == 0;
            checked[nm] = (same ? "equal to the capture, " : "DIFFERS from the capture, ") + origin + " (" + img.sourceFormat + ")";
            if (!same) test::fail(__FILE__, __LINE__, "decal texture " + nm + " from the install differs from the captured one");
        }
    }
    // VT's loose DLC files: the city-base albedo of 4.3 (frame1286, not on the four tiles) against its captured bytes.
    {
        const std::string capPath = env("C5R_DECAL_VT_CAPTURE",
                                        "F:/c5-64bit-work/0166/4109/exports/frame1286_bake/textures/5478_272734ef.bin");
        std::vector<uint8_t> cap, bytes;
        std::string origin;
        if (!readFile(capPath, cap)) {
            test::fail(__FILE__, __LINE__, "captured VT texture missing: " + capPath + " (set C5R_DECAL_VT_CAPTURE)");
        } else if (!fs.read("citybase_diff_anc.dds", bytes, &origin).ok()) {
            test::fail(__FILE__, __LINE__, "gamefs cannot resolve VT's citybase_diff_anc.dds");
        } else {
            gamefs::DdsImage img;
            bool same = gamefs::parseDds(bytes.data(), bytes.size(), img).ok();
            size_t at = 0;
            for (size_t k = 0; same && k < img.subresources.size(); ++k) {
                const gamefs::DdsSubresource& sr = img.subresources[k];
                same = at + sr.size <= cap.size() && std::memcmp(img.pixels() + sr.offset, cap.data() + at, sr.size) == 0;
                at += sr.size;
            }
            same = same && at == cap.size();
            const bool loose = origin.find("DLC/VT") != std::string::npos || origin.find("DLC\\VT") != std::string::npos;
            checked["citybase_diff_anc.dds"] = std::string(same ? "equal to the capture (frame1286), " : "DIFFERS from the capture, ") + origin;
            CHECK(same);
            CHECK(loose);
        }
    }
    for (const auto& kv : checked) std::printf("  %-36s %s\n", kv.first.c_str(), kv.second.c_str());
    for (int i = 0; i < 4; ++i) {
        RefTile t;
        if (!t.load(kTiles[i])) continue;
        RunOut a;
        if (!bakeTile(t, TexSource::GameFs, a)) continue;
        TileFigures f = compareTile(t, a, "install_textures");
        std::printf("  textures loaded through gamefs %llu\n", static_cast<unsigned long long>(a.counters.texturesLoaded));
        CHECK(f.colourEqualAll == 1.0);
        CHECK(f.specEqualAll == 1.0);
    }
}

// ---------------------------------------------------------------- ticket 0642: the loader thread

namespace {
// A synthetic install (empty Assets/ and Resource/ plus loose files) with `count` A8R8G8B8 files dec0642_<i>.dds of
// distinct colours; the names as a set would carry them (with a folder and mixed case, as the tap's names come).
bool loaderInstall(const std::string& dir, int count, std::vector<std::string>& names) {
    CreateDirectoryA(test::workDir().c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
    CreateDirectoryA((dir + "/Assets").c_str(), nullptr);
    CreateDirectoryA((dir + "/Resource").c_str(), nullptr);
    names.clear();
    for (int i = 0; i < count; ++i) {
        const std::string file = "Dec0642_" + std::to_string(i) + ".dds";
        const std::vector<uint8_t> dds = a8r8g8b8Dds(32, static_cast<uint8_t>(40 + 30 * i), static_cast<uint8_t>(200 - 25 * i),
                                                    static_cast<uint8_t>(90 + 17 * i), static_cast<uint8_t>(120 + 9 * i));
        FILE* fp = std::fopen((dir + "/Assets/" + file).c_str(), "wb");
        if (!fp) { test::fail(__FILE__, __LINE__, "cannot write " + dir + "/Assets/" + file); return false; }
        const bool ok = std::fwrite(dds.data(), 1, dds.size(), fp) == dds.size();
        std::fclose(fp);
        if (!ok) { test::fail(__FILE__, __LINE__, "short write " + file); return false; }
        names.push_back("Assets/Decals/" + file);
    }
    return true;
}

// One pass-0 decal per name over tile (0, 0), in order (later ones blend over earlier ones).
void namesSet(DecalSet& s, const std::vector<std::string>& names) {
    s = DecalSet();
    s.textures = names;
    for (size_t i = 0; i < names.size(); ++i)
        quadSet(s, 4.0f * i, 2.0f * i, 64.0f - 3.0f * i, 64.0f - i, 0, static_cast<uint32_t>(i));
}

bool waitReady(DecalPass& p, const std::vector<std::string>& names, double ms = 20000) {
    const DWORD t0 = GetTickCount();
    while (!p.ready(names)) {
        if (GetTickCount() - t0 > ms) return false;
        Sleep(1);
    }
    return true;
}

// The targets after drawing `names` with the given loader arm: 0 no loader (the draws read), 1 loader and wait for
// ready before the draw, 2 loader and draw at once (the draw waits for names in flight).
bool loaderArm(gamefs::GameFiles& fs, const std::vector<std::string>& names, int arm, UINT flags, std::vector<uint8_t>& out,
               DecalCounters& c, DecalLoaderCounters& lc) {
    Fixture f;
    if (!f.init(&fs, flags)) return false;
    DecalSet s;
    namesSet(s, names);
    if (arm > 0) {
        CHECK_OK(f.pass.startLoader());
        CHECK_EQ(f.pass.request(names), names.size());
        if (arm == 1) CHECK(waitReady(f.pass, names));
    }
    TileDecalList l;
    l.set = &s;
    for (uint32_t i = 0; i < s.decals.size(); ++i) l.indices.push_back(i);
    Status st = f.pass.drawBlend(f.d.ctx.Get(), 0, 0, l, f.targets());
    if (!st.ok()) { test::fail(__FILE__, __LINE__, "drawBlend arm " + std::to_string(arm) + ": " + st.str()); return false; }
    out = f.snapshot();
    c = f.pass.counters();
    lc = f.pass.loaderCounters();
    return true;
}
}  // namespace

C5R_TEST(game_loader_draws_bit_for_bit_what_the_draws_own_reads_draw) {
    // 0642: the same decals drawn with textures read by the draw (no loader), by the loader before the draw, by the loader
    // while the draw waits, and by the loader on a SINGLETHREADED device (created at adopt): every target byte equal; the
    // loader arms read nothing in the draw.
    std::vector<std::string> names;
    if (!loaderInstall(test::workDir() + "/decal_0642_install", 6, names)) return;
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(test::workDir() + "/decal_0642_install")));
    std::vector<uint8_t> sync, pre, during, single;
    DecalCounters c0, c1, c2, c3;
    DecalLoaderCounters l0, l1, l2, l3;
    if (!loaderArm(fs, names, 0, 0, sync, c0, l0)) return;
    if (!loaderArm(fs, names, 1, 0, pre, c1, l1)) return;
    if (!loaderArm(fs, names, 2, 0, during, c2, l2)) return;
    if (!loaderArm(fs, names, 1, D3D11_CREATE_DEVICE_SINGLETHREADED, single, c3, l3)) return;
    CHECK_EQ(c0.texturesLoaded, uint64_t(6));
    CHECK(pre == sync);
    CHECK(during == sync);
    CHECK(single == sync);
    for (const DecalCounters* c : {&c1, &c2, &c3}) {
        CHECK_EQ(c->texturesLoaded, uint64_t(0));
        CHECK_EQ(c->draws, uint64_t(6));
    }
    for (const DecalLoaderCounters* l : {&l1, &l2, &l3}) {
        CHECK_EQ(l->requested, uint64_t(6));
        CHECK_EQ(l->adopted, uint64_t(6));
        CHECK_EQ(l->failed, uint64_t(0));
        CHECK_EQ(l->syncLoads, uint64_t(0));
        CHECK(l->bytes == uint64_t(6) * (128 + 32 * 32 * 4));
    }
    CHECK_EQ(l1.waits, uint64_t(0));
    CHECK_EQ(l1.createdAtAdopt, uint64_t(0));
    CHECK_EQ(l3.createdAtAdopt, uint64_t(6));
    std::printf("  draw-while-loading arm: %llu waits, %.2f ms longest\n", static_cast<unsigned long long>(l2.waits), l2.waitMsMax);

    // the same on the real install's decal textures (riverbank, mine, grey: names of 0641's slow bake), when it is there
    gamefs::GameFiles game;
    gamefs::GameFiles::Options o;
    o.extraLooseRoots.push_back(wide(decalMods()));
    Status st = game.open(wide(decalGameDir()), o);
    if (!st.ok()) {
        test::fail(__FILE__, __LINE__, "C5R_DECAL_GAME_DIR " + decalGameDir() + ": " + st.str());
        return;
    }
    const std::vector<std::string> real = {"riverbank_d_endcap1.dds", "mine_generic_decal_s.dds", "grey.dds",
                                           "mine_generic_decal_h.dds", "riverbank_d.dds"};
    std::vector<uint8_t> rs, rl, r1;
    if (!loaderArm(game, real, 0, 0, rs, c0, l0)) return;
    if (!loaderArm(game, real, 2, 0, rl, c2, l2)) return;
    if (!loaderArm(game, real, 1, D3D11_CREATE_DEVICE_SINGLETHREADED, r1, c3, l3)) return;
    CHECK(rl == rs);
    CHECK(r1 == rs);
    CHECK_EQ(c2.texturesLoaded, uint64_t(0));
    CHECK_EQ(l2.adopted, uint64_t(5));
    std::printf("  install's textures: %llu bytes, loader reads %.1f ms (longest %.1f)\n", static_cast<unsigned long long>(l2.bytes),
                l2.readMs, l2.readMsMax);
}

C5R_TEST(game_loader_fallbacks_and_negatives) {
    std::vector<std::string> names;
    if (!loaderInstall(test::workDir() + "/decal_0642_install_b", 4, names)) return;
    gamefs::GameFiles fs;
    CHECK_OK(fs.open(wide(test::workDir() + "/decal_0642_install_b")));
    {   // before init, and without game files
        DecalPass p;
        CHECK_ERR(p.startLoader(), Err::NotFound);
        CHECK(!p.loaderOn());
        CHECK_EQ(p.request(names), size_t(0));
        CHECK(!p.ready(names));
        CHECK(p.ready(std::vector<std::string>()));
        CHECK_EQ(p.adopt(), size_t(0));
        CHECK_EQ(p.loaderPending(), size_t(0));
        p.stopLoader();   // nothing to stop
        Fixture f;
        if (!f.init()) return;
        CHECK_ERR(f.pass.startLoader(), Err::NotFound);
        CHECK(!f.pass.loaderOn());
    }
    Fixture f;
    if (!f.init(&fs)) return;
    ID3D11DeviceContext* ctx = f.d.ctx.Get();
    // without a loader request queues nothing and ready waits for nothing it cannot have
    CHECK_EQ(f.pass.request(names), size_t(0));
    CHECK(!f.pass.ready(names));
    CHECK_OK(f.pass.startLoader());
    CHECK_OK(f.pass.startLoader());   // a second start starts nothing
    CHECK(f.pass.loaderOn());
    // names 0 and 1 requested twice and in another case: queued once each
    const std::vector<std::string> first = {names[0], names[1]};
    CHECK_EQ(f.pass.request(first), size_t(2));
    CHECK_EQ(f.pass.request({"ASSETS/DECALS/DEC0642_0.DDS", names[1]}), size_t(0));
    // a name the install lacks: the loader fails it, ready() is true for it, the draw then fails NotFound as before
    const std::vector<std::string> missing = {"Assets/Decals/no_such_0642.dds"};
    CHECK_EQ(f.pass.request(missing), size_t(1));
    CHECK(waitReady(f.pass, first));
    CHECK(waitReady(f.pass, missing));
    CHECK_EQ(f.pass.loaderCounters().failed, uint64_t(1));
    CHECK_EQ(f.pass.loaderPending(), size_t(0));
    const std::vector<uint8_t> before = f.snapshot();
    DecalSet s;
    namesSet(s, missing);
    TileDecalList l;
    l.set = &s;
    l.indices = {0};
    CHECK_ERR(f.pass.drawBlend(ctx, 0, 0, l, f.targets()), Err::NotFound);
    CHECK_EQ(f.pass.counters().missingTextures, uint64_t(1));
    CHECK_EQ(f.pass.loaderCounters().syncLoads, uint64_t(1));   // the draw's own path ran (and failed) for it
    CHECK(f.snapshot() == before);
    // name 2 never requested: the draw reads it itself (counted), as before 0642
    namesSet(s, {names[0], names[2]});
    l.indices = {0, 1};
    CHECK_OK(f.pass.drawBlend(ctx, 0, 0, l, f.targets()));
    CHECK_EQ(f.pass.counters().texturesLoaded, uint64_t(1));
    CHECK_EQ(f.pass.loaderCounters().syncLoads, uint64_t(2));
    CHECK_EQ(f.pass.loaderCounters().adopted, uint64_t(2));
    // a name a draw read is not queued again
    CHECK_EQ(f.pass.request({names[2]}), size_t(0));
    // stop with names queued: they are dropped (pending 0), a later draw reads them itself; a new start queues them again
    std::vector<std::string> many;
    for (int i = 0; i < 200; ++i) many.push_back("Assets/Decals/not_there_" + std::to_string(i) + ".dds");
    const uint64_t failed0 = f.pass.loaderCounters().failed;
    CHECK_EQ(f.pass.request(many), size_t(200));
    f.pass.stopLoader();
    CHECK(!f.pass.loaderOn());
    f.pass.adopt();
    CHECK_EQ(f.pass.loaderPending(), size_t(0));
    const size_t reached = static_cast<size_t>(f.pass.loaderCounters().failed - failed0);   // all 200 lack their file
    std::printf("  stop with 200 names queued: the loader had reached %zu\n", reached);
    namesSet(s, {names[3]});
    l.indices = {0};
    CHECK_OK(f.pass.drawBlend(ctx, 0, 0, l, f.targets()));
    CHECK_EQ(f.pass.counters().texturesLoaded, uint64_t(2));
    CHECK_OK(f.pass.startLoader());
    CHECK_EQ(f.pass.request(many), 200 - reached);   // the dropped ones (those the loader had not reached) are queued again
    // the pass is destroyed with its loader running and names queued: the destructor stops it (no hang, no leak under ASan)
}

C5R_TEST_MAIN
