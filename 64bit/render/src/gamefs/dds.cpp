// render/src/gamefs/dds.cpp - DDS parser, pixel conversion and D3D11 texture creation (task 2901). Own code, written
// from Microsoft's public DDS documentation (DDS_HEADER, DDS_PIXELFORMAT, DDS_HEADER_DXT10); no DirectXTex.
#include "c5r/gamefs/dds.h"
#include <cstdio>
#include <cstring>
#include "c5r/binreader.h"

namespace c5r { namespace gamefs {

namespace {

const uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_ALPHA = 0x2, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40,
               DDPF_LUMINANCE = 0x20000, DDPF_BUMPDUDV = 0x80000;
const uint32_t DDSCAPS2_CUBEMAP = 0x200, DDSCAPS2_ALLFACES = 0xFC00, DDSCAPS2_VOLUME = 0x200000;
const uint32_t DX10_MISC_TEXTURECUBE = 0x4;

uint32_t fourcc(const char* s) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}

struct FmtInfo { DXGI_FORMAT f; const char* name; uint32_t bits; bool block; };
const FmtInfo kFormats[] = {
    {DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", 128, false},
    {DXGI_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT", 128, false},
    {DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", 64, false},
    {DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", 64, false},
    {DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM", 64, false},
    {DXGI_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT", 64, false},
    {DXGI_FORMAT_R32G32_FLOAT, "R32G32_FLOAT", 64, false},
    {DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 32, false},
    {DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", 32, false},
    {DXGI_FORMAT_R9G9B9E5_SHAREDEXP, "R9G9B9E5_SHAREDEXP", 32, false},
    {DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", 32, false},
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB", 32, false},
    {DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM", 32, false},
    {DXGI_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT", 32, false},
    {DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", 32, false},
    {DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", 32, false},
    {DXGI_FORMAT_R16G16_SNORM, "R16G16_SNORM", 32, false},
    {DXGI_FORMAT_R32_FLOAT, "R32_FLOAT", 32, false},
    {DXGI_FORMAT_R32_UINT, "R32_UINT", 32, false},
    {DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM", 32, false},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, "B8G8R8A8_UNORM_SRGB", 32, false},
    {DXGI_FORMAT_B8G8R8X8_UNORM, "B8G8R8X8_UNORM", 32, false},
    {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, "B8G8R8X8_UNORM_SRGB", 32, false},
    {DXGI_FORMAT_R8G8_UNORM, "R8G8_UNORM", 16, false},
    {DXGI_FORMAT_R8G8_SNORM, "R8G8_SNORM", 16, false},
    {DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", 16, false},
    {DXGI_FORMAT_R16_UNORM, "R16_UNORM", 16, false},
    {DXGI_FORMAT_B5G6R5_UNORM, "B5G6R5_UNORM", 16, false},
    {DXGI_FORMAT_B5G5R5A1_UNORM, "B5G5R5A1_UNORM", 16, false},
    {DXGI_FORMAT_B4G4R4A4_UNORM, "B4G4R4A4_UNORM", 16, false},
    {DXGI_FORMAT_R8_UNORM, "R8_UNORM", 8, false},
    {DXGI_FORMAT_A8_UNORM, "A8_UNORM", 8, false},
    {DXGI_FORMAT_BC1_UNORM, "BC1_UNORM", 8, true},
    {DXGI_FORMAT_BC1_UNORM_SRGB, "BC1_UNORM_SRGB", 8, true},
    {DXGI_FORMAT_BC2_UNORM, "BC2_UNORM", 16, true},
    {DXGI_FORMAT_BC2_UNORM_SRGB, "BC2_UNORM_SRGB", 16, true},
    {DXGI_FORMAT_BC3_UNORM, "BC3_UNORM", 16, true},
    {DXGI_FORMAT_BC3_UNORM_SRGB, "BC3_UNORM_SRGB", 16, true},
    {DXGI_FORMAT_BC4_UNORM, "BC4_UNORM", 8, true},
    {DXGI_FORMAT_BC4_SNORM, "BC4_SNORM", 8, true},
    {DXGI_FORMAT_BC5_UNORM, "BC5_UNORM", 16, true},
    {DXGI_FORMAT_BC5_SNORM, "BC5_SNORM", 16, true},
    {DXGI_FORMAT_BC6H_UF16, "BC6H_UF16", 16, true},
    {DXGI_FORMAT_BC6H_SF16, "BC6H_SF16", 16, true},
    {DXGI_FORMAT_BC7_UNORM, "BC7_UNORM", 16, true},
    {DXGI_FORMAT_BC7_UNORM_SRGB, "BC7_UNORM_SRGB", 16, true},
};

const FmtInfo* info(DXGI_FORMAT f) {
    for (const FmtInfo& i : kFormats) {
        if (i.f == f) return &i;
    }
    return nullptr;
}

// A legacy mask format that is expanded to B8G8R8A8 (one generic routine driven by the masks).
struct MaskConv {
    uint32_t bits = 0, r = 0, g = 0, b = 0, a = 0;
    bool lum = false;
};

struct Decision {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool convert = false;
    MaskConv mc;
    std::string name;
};

bool masks(uint32_t pr, uint32_t pg, uint32_t pb, uint32_t pa, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return pr == r && pg == g && pb == b && pa == a;
}

// Maps a legacy DDS_PIXELFORMAT to a DXGI format or a conversion. Unsupported if neither.
Status decideLegacy(uint32_t pfFlags, uint32_t cc, uint32_t bits, uint32_t r, uint32_t g, uint32_t b, uint32_t a,
                    Decision& d) {
    if (pfFlags & DDPF_FOURCC) {
        struct CC { const char* s; DXGI_FORMAT f; };
        static const CC table[] = {
            {"DXT1", DXGI_FORMAT_BC1_UNORM}, {"DXT2", DXGI_FORMAT_BC2_UNORM}, {"DXT3", DXGI_FORMAT_BC2_UNORM},
            {"DXT4", DXGI_FORMAT_BC3_UNORM}, {"DXT5", DXGI_FORMAT_BC3_UNORM}, {"ATI1", DXGI_FORMAT_BC4_UNORM},
            {"BC4U", DXGI_FORMAT_BC4_UNORM}, {"BC4S", DXGI_FORMAT_BC4_SNORM}, {"ATI2", DXGI_FORMAT_BC5_UNORM},
            {"BC5U", DXGI_FORMAT_BC5_UNORM}, {"BC5S", DXGI_FORMAT_BC5_SNORM},
        };
        for (const CC& c : table) {
            if (cc == fourcc(c.s)) {
                d.format = c.f;
                d.name = c.s;
                return Status::Ok();
            }
        }
        struct Num { uint32_t n; DXGI_FORMAT f; };
        static const Num nums[] = {
            {36, DXGI_FORMAT_R16G16B16A16_UNORM}, {110, DXGI_FORMAT_R16G16B16A16_SNORM}, {111, DXGI_FORMAT_R16_FLOAT},
            {112, DXGI_FORMAT_R16G16_FLOAT}, {113, DXGI_FORMAT_R16G16B16A16_FLOAT}, {114, DXGI_FORMAT_R32_FLOAT},
            {115, DXGI_FORMAT_R32G32_FLOAT}, {116, DXGI_FORMAT_R32G32B32A32_FLOAT},
        };
        for (const Num& n : nums) {
            if (cc == n.n) {
                d.format = n.f;
                d.name = "D3DFMT_" + std::to_string(n.n);
                return Status::Ok();
            }
        }
        return Status::fail(Err::Unsupported, "dds: FourCC 0x" + [&] {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%08X", cc);
            return std::string(buf);
        }() + " not supported");
    }
    if (pfFlags & DDPF_BUMPDUDV) return Status::fail(Err::Unsupported, "dds: bump-map (DUDV) formats not supported");
    auto conv = [&](const char* name, bool lum) {
        d.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.convert = true;
        d.name = name;
        d.mc.bits = bits;
        d.mc.r = r;
        d.mc.g = g;
        d.mc.b = b;
        d.mc.a = (pfFlags & (DDPF_ALPHAPIXELS | DDPF_ALPHA)) ? a : 0;
        d.mc.lum = lum;
        return Status::Ok();
    };
    auto direct = [&](DXGI_FORMAT f, const char* name) {
        d.format = f;
        d.name = name;
        return Status::Ok();
    };
    const uint32_t aa = (pfFlags & (DDPF_ALPHAPIXELS | DDPF_ALPHA)) ? a : 0;
    if (pfFlags & DDPF_RGB) {
        switch (bits) {
        case 32:
            if (masks(r, g, b, aa, 0xff0000, 0xff00, 0xff, 0xff000000)) return direct(DXGI_FORMAT_B8G8R8A8_UNORM, "A8R8G8B8");
            if (masks(r, g, b, aa, 0xff0000, 0xff00, 0xff, 0)) return direct(DXGI_FORMAT_B8G8R8X8_UNORM, "X8R8G8B8");
            if (masks(r, g, b, aa, 0xff, 0xff00, 0xff0000, 0xff000000)) return direct(DXGI_FORMAT_R8G8B8A8_UNORM, "A8B8G8R8");
            if (masks(r, g, b, aa, 0xff, 0xff00, 0xff0000, 0)) return conv("X8B8G8R8", false);
            if (masks(r, g, b, aa, 0xffff, 0xffff0000, 0, 0)) return direct(DXGI_FORMAT_R16G16_UNORM, "G16R16");
            if (masks(r, g, b, aa, 0x3ff, 0xffc00, 0x3ff00000, 0xc0000000))
                return direct(DXGI_FORMAT_R10G10B10A2_UNORM, "A2B10G10R10");
            break;
        case 24:
            if (masks(r, g, b, aa, 0xff0000, 0xff00, 0xff, 0)) return conv("R8G8B8", false);
            break;
        case 16:
            if (masks(r, g, b, aa, 0xf800, 0x7e0, 0x1f, 0)) return conv("R5G6B5", false);
            if (masks(r, g, b, aa, 0x7c00, 0x3e0, 0x1f, 0x8000)) return conv("A1R5G5B5", false);
            if (masks(r, g, b, aa, 0x7c00, 0x3e0, 0x1f, 0)) return conv("X1R5G5B5", false);
            if (masks(r, g, b, aa, 0xf00, 0xf0, 0xf, 0xf000)) return conv("A4R4G4B4", false);
            if (masks(r, g, b, aa, 0xf00, 0xf0, 0xf, 0)) return conv("X4R4G4B4", false);
            if (masks(r, g, b, aa, 0xe0, 0x1c, 0x3, 0xff00)) return conv("A8R3G3B2", false);
            break;
        case 8:
            if (masks(r, g, b, aa, 0xe0, 0x1c, 0x3, 0)) return conv("R3G3B2", false);
            break;
        default:
            break;
        }
    } else if (pfFlags & DDPF_LUMINANCE) {
        if (bits == 8 && r == 0xff && aa == 0) return direct(DXGI_FORMAT_R8_UNORM, "L8");
        if (bits == 16 && r == 0xffff && aa == 0) return direct(DXGI_FORMAT_R16_UNORM, "L16");
        if (bits == 16 && r == 0xff && aa == 0xff00) return direct(DXGI_FORMAT_R8G8_UNORM, "A8L8");
        if (bits == 8 && r == 0xf && aa == 0xf0) return conv("A4L4", true);
    } else if ((pfFlags & DDPF_ALPHA) && bits == 8 && a == 0xff) {
        return direct(DXGI_FORMAT_A8_UNORM, "A8");
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "dds: pixel format flags 0x%X bits %u masks %08X/%08X/%08X/%08X not supported", pfFlags,
                  bits, r, g, b, a);
    return Status::fail(Err::Unsupported, buf);
}

// Expands one channel value under a mask to 8 bits.
uint8_t expand(uint32_t v, uint32_t mask) {
    if (!mask) return 0;
    unsigned shift = 0;
    while (!((mask >> shift) & 1u)) ++shift;
    uint32_t m = mask >> shift;
    uint32_t x = (v & mask) >> shift;
    return static_cast<uint8_t>((x * 255u + m / 2) / m);
}

void convertPixels(const MaskConv& mc, const uint8_t* src, size_t count, uint8_t* dst) {
    const uint32_t bpp = mc.bits / 8;
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < bpp; ++k) v |= static_cast<uint32_t>(src[i * bpp + k]) << (8 * k);
        uint8_t R = expand(v, mc.r);
        uint8_t G = mc.lum ? R : expand(v, mc.g);
        uint8_t B = mc.lum ? R : expand(v, mc.b);
        uint8_t A = mc.a ? expand(v, mc.a) : 255;
        dst[i * 4 + 0] = B;
        dst[i * 4 + 1] = G;
        dst[i * 4 + 2] = R;
        dst[i * 4 + 3] = A;
    }
}

uint32_t maxMips(uint32_t w, uint32_t h, uint32_t d) {
    uint32_t m = w;
    if (h > m) m = h;
    if (d > m) m = d;
    uint32_t n = 1;
    while (m > 1) {
        m >>= 1;
        ++n;
    }
    return n;
}

} // namespace

std::string dxgiFormatName(DXGI_FORMAT f) {
    const FmtInfo* i = info(f);
    return i ? std::string(i->name) : "DXGI_" + std::to_string(static_cast<int>(f));
}

uint32_t formatBits(DXGI_FORMAT f, bool* isBlock) {
    const FmtInfo* i = info(f);
    if (isBlock) *isBlock = i ? i->block : false;
    return i ? i->bits : 0;
}

Status parseDds(const uint8_t* data, size_t size, DdsImage& out) {
    out = DdsImage();
    if (!data || size < 4) return Status::fail(Err::Truncated, "dds: fewer than 4 bytes");
    if (std::memcmp(data, "DDS ", 4) != 0) return Status::fail(Err::BadMagic, "dds: magic is not 'DDS '");
    if (size < 128) return Status::fail(Err::Truncated, "dds: header needs 128 bytes, have " + std::to_string(size));
    BinReader r(data, size);
    r.seek(4);
    uint32_t hdrSize = r.u32();
    r.u32();  // flags: not needed, the caps and pixel format decide
    uint32_t height = r.u32();
    uint32_t width = r.u32();
    r.u32();  // pitch or linear size: not trusted, layouts are computed
    uint32_t depth = r.u32();
    uint32_t mips = r.u32();
    r.seek(76);
    uint32_t pfSize = r.u32(), pfFlags = r.u32(), cc = r.u32(), bits = r.u32();
    uint32_t rm = r.u32(), gm = r.u32(), bm = r.u32(), am = r.u32();
    r.u32();  // caps
    uint32_t caps2 = r.u32();
    // 124 is the documented size. 24 is accepted as a known exporter quirk: 28 DXT1 files of the install
    // (Expansion1/2BuildingsTextures.fpk, e.g. alhambra_hb_diff.dds) carry it with an otherwise valid header.
    if (hdrSize != 124 && hdrSize != 24)
        return Status::fail(Err::BadLayout, "dds: header size " + std::to_string(hdrSize) + ", expected 124");
    if (pfSize != 32) return Status::fail(Err::BadLayout, "dds: pixel format size " + std::to_string(pfSize) + ", expected 32");

    size_t dataStart = 128;
    Decision dec;
    uint32_t arraySize = 1;
    bool cube = false;
    DdsDim dim = DdsDim::Tex2D;
    if ((pfFlags & DDPF_FOURCC) && cc == fourcc("DX10")) {
        if (size < 148) return Status::fail(Err::Truncated, "dds: DX10 header needs 148 bytes, have " + std::to_string(size));
        r.seek(128);
        uint32_t dxgi = r.u32(), resDim = r.u32(), misc = r.u32();
        arraySize = r.u32();
        dataStart = 148;
        const FmtInfo* fi = info(static_cast<DXGI_FORMAT>(dxgi));
        if (!fi) return Status::fail(Err::Unsupported, "dds: DXGI format " + std::to_string(dxgi) + " not supported");
        dec.format = fi->f;
        dec.name = "DX10";
        if (arraySize == 0) return Status::fail(Err::BadLayout, "dds: DX10 array size 0");
        if (resDim == 2) {
            dim = DdsDim::Tex1D;
            if (height != 1 && height != 0) return Status::fail(Err::BadLayout, "dds: 1D texture with height " + std::to_string(height));
            height = 1;
        } else if (resDim == 3) {
            dim = DdsDim::Tex2D;
            if (misc & DX10_MISC_TEXTURECUBE) {
                cube = true;
                if (arraySize > kMaxArray / 6) return Status::fail(Err::Unsupported, "dds: cube array too large");
                arraySize *= 6;
            }
        } else if (resDim == 4) {
            dim = DdsDim::Tex3D;
            if (arraySize != 1) return Status::fail(Err::BadLayout, "dds: 3D texture with array size " + std::to_string(arraySize));
            if (misc & DX10_MISC_TEXTURECUBE) return Status::fail(Err::BadLayout, "dds: 3D texture marked as cube");
        } else {
            return Status::fail(Err::BadLayout, "dds: DX10 resource dimension " + std::to_string(resDim));
        }
        if (dim != DdsDim::Tex3D) depth = 1;
    } else {
        C5R_TRY(decideLegacy(pfFlags, cc, bits, rm, gm, bm, am, dec));
        if (caps2 & DDSCAPS2_VOLUME) {
            if (caps2 & DDSCAPS2_CUBEMAP) return Status::fail(Err::BadLayout, "dds: both volume and cube map");
            dim = DdsDim::Tex3D;
        } else {
            depth = 1;
            if (caps2 & DDSCAPS2_CUBEMAP) {
                if ((caps2 & DDSCAPS2_ALLFACES) != DDSCAPS2_ALLFACES)
                    return Status::fail(Err::Unsupported, "dds: cube map without all six faces");
                cube = true;
                arraySize = 6;
            }
        }
    }
    if (width == 0 || height == 0 || depth == 0)
        return Status::fail(Err::BadLayout, "dds: zero dimension " + std::to_string(width) + "x" + std::to_string(height) +
                                                "x" + std::to_string(depth));
    const uint32_t lim = dim == DdsDim::Tex3D ? kMaxTex3D : (dim == DdsDim::Tex1D ? kMaxTex1D : kMaxTex2D);
    if (width > lim || height > lim || (dim == DdsDim::Tex3D && depth > lim) || arraySize > kMaxArray)
        return Status::fail(Err::Unsupported, "dds: dimensions " + std::to_string(width) + "x" + std::to_string(height) + "x" +
                                                  std::to_string(depth) + " exceed the D3D11 limit " + std::to_string(lim));
    if (mips == 0) mips = 1;
    const uint32_t mm = maxMips(width, height, depth);
    if (mips > mm)
        return Status::fail(Err::BadLayout, "dds: " + std::to_string(mips) + " mips for " + std::to_string(width) + "x" +
                                                std::to_string(height) + "x" + std::to_string(depth) + " (max " +
                                                std::to_string(mm) + ")");
    bool block = false;
    uint32_t fbits = formatBits(dec.format, &block);
    if (block && ((width % 4) || (height % 4)))
        return Status::fail(Err::Unsupported, "dds: block-compressed top level " + std::to_string(width) + "x" +
                                                  std::to_string(height) + " is not a multiple of 4");
    // Source layout in the file.
    const uint32_t srcBits = dec.convert ? dec.mc.bits : fbits;
    out.format = dec.format;
    out.width = width;
    out.height = height;
    out.depth = depth;
    out.mipLevels = mips;
    out.arraySize = arraySize;
    out.cube = cube;
    out.dim = dim;
    out.sourceFormat = dec.name;
    out.converted = dec.convert;
    out.src = data;
    size_t srcPos = dataStart, dstPos = 0;
    std::vector<std::pair<size_t, size_t>> srcRanges;  // for conversion: (src offset, pixel count)
    for (uint32_t s = 0; s < arraySize; ++s) {
        for (uint32_t m = 0; m < mips; ++m) {
            uint32_t w = width >> m, h = height >> m, dd = depth >> m;
            if (!w) w = 1;
            if (!h) h = 1;
            if (!dd) dd = 1;
            uint64_t row, rows;
            if (block) {
                row = static_cast<uint64_t>((w + 3) / 4) * (fbits);  // fbits holds bytes per block for BC formats
                rows = (h + 3) / 4;
            } else {
                row = (static_cast<uint64_t>(w) * srcBits + 7) / 8;
                rows = h;
            }
            uint64_t srcSize = row * rows * dd;
            if (srcPos + srcSize > size)
                return Status::fail(Err::Truncated, "dds: pixel data ends at " + std::to_string(size) + ", slice " +
                                                        std::to_string(s) + " mip " + std::to_string(m) + " needs up to " +
                                                        std::to_string(srcPos + srcSize));
            DdsSubresource sr;
            sr.width = w;
            sr.height = h;
            sr.depth = dd;
            if (dec.convert) {
                sr.rowPitch = w * 4;
                sr.slicePitch = sr.rowPitch * h;
                sr.size = static_cast<size_t>(sr.slicePitch) * dd;
                sr.offset = dstPos;
                dstPos += sr.size;
                srcRanges.emplace_back(srcPos, static_cast<size_t>(w) * h * dd);
            } else {
                sr.rowPitch = static_cast<uint32_t>(row);
                sr.slicePitch = static_cast<uint32_t>(row * rows);
                sr.size = static_cast<size_t>(srcSize);
                sr.offset = srcPos;
            }
            srcPos += static_cast<size_t>(srcSize);
            out.subresources.push_back(sr);
        }
    }
    if (dec.convert) {
        out.bytes.resize(dstPos);
        for (size_t i = 0; i < srcRanges.size(); ++i)
            convertPixels(dec.mc, data + srcRanges[i].first, srcRanges[i].second, out.bytes.data() + out.subresources[i].offset);
    }
    return Status::Ok();
}

static std::string hr(HRESULT h) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", static_cast<unsigned>(h));
    return b;
}

Status createTexture(ID3D11Device* dev, const DdsImage& img, ID3D11Resource** tex, ID3D11ShaderResourceView** srv) {
    if (tex) *tex = nullptr;
    if (srv) *srv = nullptr;
    if (!dev || !tex) return Status::fail(Err::Device, "createTexture: no device or no output");
    if (img.subresources.size() != static_cast<size_t>(img.arraySize) * img.mipLevels || !img.pixels())
        return Status::fail(Err::BadLayout, "createTexture: image has no pixel data or wrong subresource count");
    UINT support = 0;
    HRESULT h = dev->CheckFormatSupport(img.format, &support);
    UINT need = img.dim == DdsDim::Tex3D ? D3D11_FORMAT_SUPPORT_TEXTURE3D
              : img.dim == DdsDim::Tex1D ? D3D11_FORMAT_SUPPORT_TEXTURE1D
              : img.cube ? D3D11_FORMAT_SUPPORT_TEXTURECUBE : D3D11_FORMAT_SUPPORT_TEXTURE2D;
    if (FAILED(h) || !(support & need))
        return Status::fail(Err::Device, "createTexture: device does not support " + dxgiFormatName(img.format) + " for this "
                                         "dimension (" + hr(h) + ")");
    std::vector<D3D11_SUBRESOURCE_DATA> init(img.subresources.size());
    const uint8_t* base = img.pixels();
    for (size_t i = 0; i < init.size(); ++i) {
        init[i].pSysMem = base + img.subresources[i].offset;
        init[i].SysMemPitch = img.subresources[i].rowPitch;
        init[i].SysMemSlicePitch = img.subresources[i].slicePitch;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = img.format;
    if (img.dim == DdsDim::Tex3D) {
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = img.width;
        d.Height = img.height;
        d.Depth = img.depth;
        d.MipLevels = img.mipLevels;
        d.Format = img.format;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture3D* t = nullptr;
        h = dev->CreateTexture3D(&d, init.data(), &t);
        if (FAILED(h)) return Status::fail(Err::Device, "CreateTexture3D failed " + hr(h));
        *tex = t;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        sd.Texture3D.MipLevels = img.mipLevels;
    } else if (img.dim == DdsDim::Tex1D) {
        D3D11_TEXTURE1D_DESC d = {};
        d.Width = img.width;
        d.MipLevels = img.mipLevels;
        d.ArraySize = img.arraySize;
        d.Format = img.format;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture1D* t = nullptr;
        h = dev->CreateTexture1D(&d, init.data(), &t);
        if (FAILED(h)) return Status::fail(Err::Device, "CreateTexture1D failed " + hr(h));
        *tex = t;
        if (img.arraySize > 1) {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
            sd.Texture1DArray.MipLevels = img.mipLevels;
            sd.Texture1DArray.ArraySize = img.arraySize;
        } else {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
            sd.Texture1D.MipLevels = img.mipLevels;
        }
    } else {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = img.width;
        d.Height = img.height;
        d.MipLevels = img.mipLevels;
        d.ArraySize = img.arraySize;
        d.Format = img.format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = img.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        ID3D11Texture2D* t = nullptr;
        h = dev->CreateTexture2D(&d, init.data(), &t);
        if (FAILED(h)) return Status::fail(Err::Device, "CreateTexture2D failed " + hr(h));
        *tex = t;
        if (img.cube) {
            if (img.arraySize > 6) {
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                sd.TextureCubeArray.MipLevels = img.mipLevels;
                sd.TextureCubeArray.NumCubes = img.arraySize / 6;
            } else {
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                sd.TextureCube.MipLevels = img.mipLevels;
            }
        } else if (img.arraySize > 1) {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Texture2DArray.MipLevels = img.mipLevels;
            sd.Texture2DArray.ArraySize = img.arraySize;
        } else {
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = img.mipLevels;
        }
    }
    if (srv) {
        h = dev->CreateShaderResourceView(*tex, &sd, srv);
        if (FAILED(h)) {
            (*tex)->Release();
            *tex = nullptr;
            return Status::fail(Err::Device, "CreateShaderResourceView failed " + hr(h));
        }
    }
    return Status::Ok();
}

}} // namespace c5r::gamefs
