// render/src/bakepost/style.cpp - CacheBake's per-style inputs read from the player's install (issue 3403): the Sunlight
// and Shadowlight cubes and the Terrain ColorKey LUT. Names and the LUT rule: terrain-bake-passes.md section 5.2
// (names [C] from the EXE's strings, the LUT texel order [L] against the capture). Interface: c5r/bake/post.h.
#include "c5r/bake/post.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"

namespace c5r { namespace bake {

namespace {

const char* const kStyles[5] = {"Generic", "America", "Asia", "Africa", "Europe"};

Status hr(HRESULT h, const char* what) {
    if (SUCCEEDED(h)) return Status::Ok();
    char b[32];
    std::snprintf(b, sizeof b, " (hr 0x%08lx)", static_cast<unsigned long>(h));
    return Status::fail(Err::Device, std::string(what) + b);
}

DXGI_FORMAT typeless(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT srgb(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return f;
    }
}

// A file of a style: its bytes, or the read's failure as loadStyleInputs reports it (NotFound naming the file).
Status readStyleFile(const gamefs::GameFiles& fs, const std::string& name, std::vector<uint8_t>& bytes) {
    bytes.clear();
    Status st = fs.read(name, bytes);
    if (!st.ok()) {
        bytes.clear();
        return Status::fail(Err::NotFound, name + ": " + st.str());
    }
    return Status::Ok();
}

// A cube from a DDS file's bytes, with the sRGB view the EXE's sampler id 2 takes (asset-pipeline.md 3.1, "2nd view").
Status createCube(ID3D11Device* dev, const std::string& name, const std::vector<uint8_t>& bytes,
                  ID3D11ShaderResourceView** out) {
    gamefs::DdsImage img;
    C5R_TRY(gamefs::parseDds(bytes.data(), bytes.size(), img));
    if (!img.cube || img.arraySize != 6 || img.dim != gamefs::DdsDim::Tex2D)
        return Status::fail(Err::BadLayout, name + " is not a cube map");
    const DXGI_FORMAT tf = typeless(img.format);
    if (tf == DXGI_FORMAT_UNKNOWN) return Status::fail(Err::Unsupported, name + ": format without an sRGB view");
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = img.width;
    d.Height = img.height;
    d.MipLevels = img.mipLevels;
    d.ArraySize = 6;
    d.Format = tf;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const gamefs::DdsSubresource& s : img.subresources)
        init.push_back(D3D11_SUBRESOURCE_DATA{img.pixels() + s.offset, s.rowPitch, s.slicePitch});
    if (init.size() != static_cast<size_t>(img.mipLevels) * 6) return Status::fail(Err::BadLayout, name + ": subresource count");
    ID3D11Texture2D* tex = nullptr;
    C5R_TRY(hr(dev->CreateTexture2D(&d, init.data(), &tex), "CreateTexture2D (style cube)"));
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = srgb(img.format);
    v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    v.TextureCube.MipLevels = img.mipLevels;
    const HRESULT h = dev->CreateShaderResourceView(tex, &v, out);
    tex->Release();
    return hr(h, "CreateShaderResourceView (style cube)");
}

}  // namespace

const char* styleName(uint32_t style) { return style < 5 ? kStyles[style] : nullptr; }

std::string sunlightFile(uint32_t style) {
    return style < 5 ? std::string("Sunlight_") + kStyles[style] + "_Terrain.dds" : std::string();
}

std::string shadowlightFile(uint32_t style) {
    // America, Asia and Europe use the "_2" file (the EXE's name table).
    if (style >= 5) return std::string();
    const bool two = style == 1 || style == 2 || style == 4;
    return std::string("Shadowlight_") + kStyles[style] + (two ? "_Terrain_2.dds" : "_Terrain.dds");
}

std::string colorKeyFile(uint32_t style) {
    return style < 5 ? std::string("Terrain_") + kStyles[style] + "_ColorKey.tga" : std::string();
}

Status buildColorKeyLut(const uint8_t* tga, size_t size, std::vector<uint8_t>& rgba) {
    rgba.clear();
    if (!tga || size < 18) return Status::fail(Err::Truncated, "ColorKey TGA: no header");
    const uint32_t idLen = tga[0], cmType = tga[1], type = tga[2];
    const uint32_t w = tga[12] | tga[13] << 8, h = tga[14] | tga[15] << 8, bpp = tga[16], desc = tga[17];
    if (cmType != 0 || type != 2) return Status::fail(Err::Unsupported, "ColorKey TGA: only uncompressed true-colour (type 2)");
    if (bpp != 32) return Status::fail(Err::Unsupported, "ColorKey TGA: only 32 bits per pixel");
    if (desc & 0x10) return Status::fail(Err::Unsupported, "ColorKey TGA: right-to-left pixel order");
    const size_t need = static_cast<size_t>(w) * h;
    if (need < kColorKeyTexels) return Status::fail(Err::BadLayout, "ColorKey TGA: fewer than 32^3 pixels");
    if (size < 18 + idLen + need * 4) return Status::fail(Err::Truncated, "ColorKey TGA: pixel data ends early");
    const uint8_t* px = tga + 18 + idLen;
    const bool topDown = (desc & 0x20) != 0;
    rgba.resize(kColorKeyTexels * 4);
    // The image in reading order (top row first), each pixel BGRA -> RGBA; the first 32^3 pixels fill the volume with
    // x fastest, then y, then z.
    for (size_t i = 0; i < kColorKeyTexels; ++i) {
        const size_t row = i / w, col = i % w;
        const size_t fileRow = topDown ? row : (h - 1 - row);
        const uint8_t* p = px + (fileRow * w + col) * 4;
        rgba[i * 4 + 0] = p[2];
        rgba[i * 4 + 1] = p[1];
        rgba[i * 4 + 2] = p[0];
        rgba[i * 4 + 3] = p[3];
    }
    return Status::Ok();
}

void StyleInputs::release() {
    if (sunCube) sunCube->Release();
    if (shadowCube) shadowCube->Release();
    if (colorKey) colorKey->Release();
    sunCube = shadowCube = colorKey = nullptr;
}

Status readStyleFiles(const gamefs::GameFiles& fs, uint32_t style, StyleFiles& out) {
    out = StyleFiles();
    out.style = style;
    if (style >= 5) {
        out.sunRead = out.shadowRead = out.colorKeyRead = Status::fail(Err::BadLayout, "style " + std::to_string(style) + " is not 0..4");
        return out.sunRead;
    }
    out.sunRead = readStyleFile(fs, sunlightFile(style), out.sun);
    out.shadowRead = readStyleFile(fs, shadowlightFile(style), out.shadow);
    out.colorKeyRead = readStyleFile(fs, colorKeyFile(style), out.colorKey);
    if (!out.sunRead.ok()) return out.sunRead;
    if (!out.shadowRead.ok()) return out.shadowRead;
    return out.colorKeyRead;
}

Status loadStyleInputs(ID3D11Device* dev, const gamefs::GameFiles& fs, uint32_t style, StyleInputs& out) {
    out.release();
    if (!dev) return Status::fail(Err::BadLayout, "style inputs: no device");
    if (style >= 5) return Status::fail(Err::BadLayout, "style " + std::to_string(style) + " is not 0..4");
    StyleFiles f;
    readStyleFiles(fs, style, f);
    return createStyleInputs(dev, f, out);
}

Status createStyleInputs(ID3D11Device* dev, const StyleFiles& f, StyleInputs& out) {
    out.release();
    if (!dev) return Status::fail(Err::BadLayout, "style inputs: no device");
    const uint32_t style = f.style;
    if (style >= 5) return Status::fail(Err::BadLayout, "style " + std::to_string(style) + " is not 0..4");
    // the order of the checks as before ticket 0698 (each file read, then made): the first failure is the one reported
    StyleInputs s;
    Status st = f.sunRead;
    if (st.ok()) st = createCube(dev, sunlightFile(style), f.sun, &s.sunCube);
    if (st.ok()) st = f.shadowRead;
    if (st.ok()) st = createCube(dev, shadowlightFile(style), f.shadow, &s.shadowCube);
    std::vector<uint8_t> lut;
    if (st.ok()) st = f.colorKeyRead;
    if (st.ok()) st = buildColorKeyLut(f.colorKey.data(), f.colorKey.size(), lut);
    if (st.ok()) {
        D3D11_TEXTURE3D_DESC d = {32, 32, 32, 1, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA init = {lut.data(), 32 * 4, 32 * 32 * 4};
        ID3D11Texture3D* t = nullptr;
        st = hr(dev->CreateTexture3D(&d, &init, &t), "CreateTexture3D (ColorKey)");
        if (st.ok()) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
            v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            v.Texture3D.MipLevels = 1;
            st = hr(dev->CreateShaderResourceView(t, &v, &s.colorKey), "CreateShaderResourceView (ColorKey)");
            t->Release();
        }
    }
    if (!st.ok()) {
        s.release();
        return st;
    }
    out = s;
    return Status::Ok();
}

}} // namespace c5r::bake
