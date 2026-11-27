// render/src/terrainsets/lean.cpp - the EXE's `.lean` bump textures (issue 3602). Format and rules: c5r/terrainsets/lean.h
// and research/05-graphics-composition/water-system.md section 8. Own code.
#include "c5r/terrainsets/lean.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace terrainsets {

namespace {

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

} // namespace

uint64_t leanTexels(uint32_t width, uint32_t height, uint32_t mips) {
    uint64_t n = 0;
    for (uint32_t m = 0; m < mips && m < 32; ++m) {
        const uint64_t w = (width >> m) ? (width >> m) : 1;
        const uint64_t h = (height >> m) ? (height >> m) : 1;
        n += w * h;
    }
    return n;
}

uint32_t leanFullChain(uint32_t width, uint32_t height) {
    uint32_t m = 1, s = width > height ? width : height;
    while (s > 1) {
        s >>= 1;
        ++m;
    }
    return m;
}

Status parseLean(const uint8_t* p, size_t n, LeanFile& out) {
    out = LeanFile();
    if (!p || n < kLeanHeaderSize)
        return Status::fail(Err::Truncated, "lean: " + std::to_string(n) + " bytes, the header needs 36");
    if (std::memcmp(p, "LEAN", 4) != 0) return Status::fail(Err::BadMagic, "lean: no LEAN magic");
    LeanFile f;
    std::memcpy(&f.unknown04, p + 4, 4);
    f.width = rd32(p + 8);
    f.height = rd32(p + 0xc);
    f.mips = rd32(p + 0x10);
    f.bytesPerChannel = rd32(p + 0x14);
    f.offFour = rd32(p + 0x18);
    f.offOne = rd32(p + 0x1c);
    f.byte20 = p[0x20];
    if (f.width == 0 || f.height == 0 || f.width > kLeanMaxSize || f.height > kLeanMaxSize)
        return Status::fail(Err::BadLayout, "lean: size " + std::to_string(f.width) + " x " + std::to_string(f.height));
    if (f.mips == 0 || f.mips > leanFullChain(f.width, f.height))
        return Status::fail(Err::BadLayout, "lean: " + std::to_string(f.mips) + " mips for " + std::to_string(f.width) +
                                                " x " + std::to_string(f.height));
    if (f.bytesPerChannel != 1 && f.bytesPerChannel != 2)
        return Status::fail(Err::Unsupported, "lean: " + std::to_string(f.bytesPerChannel) + " bytes per channel");
    const uint64_t texels = leanTexels(f.width, f.height, f.mips);
    const uint64_t four = texels * 4 * f.bytesPerChannel, one = texels * f.bytesPerChannel;
    if (f.offFour < kLeanHeaderSize || f.offOne < kLeanHeaderSize)
        return Status::fail(Err::BadLayout, "lean: a block offset lies inside the header");
    if (f.offOne < f.offFour + four)
        return Status::fail(Err::BadLayout, "lean: the one-channel block does not follow the four-channel block");
    if (f.offFour + four > n || f.offOne + one > n)
        return Status::fail(Err::Truncated, "lean: the blocks need " + std::to_string(f.offOne + one) + " bytes, the file has " +
                                                std::to_string(n));
    f.four = p + f.offFour;
    f.fourBytes = static_cast<size_t>(four);
    f.one = p + f.offOne;
    f.oneBytes = static_cast<size_t>(one);
    f.fourFormat = f.bytesPerChannel == 2 ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    f.oneFormat = f.bytesPerChannel == 2 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    out = f;
    return Status::Ok();
}

Status leanMip(const LeanFile& f, bool fourChannel, uint32_t mip, LeanMip& out) {
    out = LeanMip();
    if (!f.four || !f.one) return Status::fail(Err::BadLayout, "lean: not parsed");
    if (mip >= f.mips) return Status::fail(Err::BadLayout, "lean: mip " + std::to_string(mip) + " of " + std::to_string(f.mips));
    const uint32_t texelBytes = (fourChannel ? 4 : 1) * f.bytesPerChannel;
    out.data = (fourChannel ? f.four : f.one) + leanTexels(f.width, f.height, mip) * texelBytes;
    out.width = (f.width >> mip) ? (f.width >> mip) : 1;
    out.height = (f.height >> mip) ? (f.height >> mip) : 1;
    out.rowPitch = out.width * texelBytes;
    return Status::Ok();
}

Status createLeanTextures(ID3D11Device* dev, const LeanFile& f, ID3D11Texture2D** four, ID3D11Texture2D** one) {
    if (!dev || !four || !one) return Status::fail(Err::BadLayout, "lean: device or output null");
    *four = nullptr;
    *one = nullptr;
    if (!f.four || !f.one) return Status::fail(Err::BadLayout, "lean: not parsed");
    ID3D11Texture2D* t[2] = {nullptr, nullptr};
    for (int k = 0; k < 2; ++k) {
        std::vector<D3D11_SUBRESOURCE_DATA> init(f.mips);
        for (uint32_t m = 0; m < f.mips; ++m) {
            LeanMip lm;
            C5R_TRY(leanMip(f, k == 0, m, lm));
            init[m].pSysMem = lm.data;
            init[m].SysMemPitch = lm.rowPitch;
            init[m].SysMemSlicePitch = lm.rowPitch * lm.height;
        }
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = f.width;
        d.Height = f.height;
        d.MipLevels = f.mips;
        d.ArraySize = 1;
        d.Format = k == 0 ? f.fourFormat : f.oneFormat;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const HRESULT hr = dev->CreateTexture2D(&d, init.data(), &t[k]);
        if (FAILED(hr)) {
            rel(t[0]);
            rel(t[1]);
            char b[16];
            std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(hr));
            return Status::fail(Err::Device, std::string("lean: CreateTexture2D ") + b);
        }
    }
    *four = t[0];
    *one = t[1];
    return Status::Ok();
}

Status loadLean(const gamefs::GameFiles& fs, const std::string& name, std::vector<uint8_t>& bytes, LeanFile& out) {
    out = LeanFile();
    C5R_TRY(fs.read(name, bytes));
    Status st = parseLean(bytes.data(), bytes.size(), out);
    if (!st.ok()) st.msg = name + ": " + st.msg;
    return st;
}

}} // namespace c5r::terrainsets
