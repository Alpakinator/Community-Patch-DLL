// c5r/gamefs/dds.h - DDS parser and D3D11 texture creation (task 2901, epic 2900).
//
// parseDds() reads a whole .dds file held in memory and describes its subresources in D3D11 order (array slice
// major, mip minor; a cube's six faces are array slices +X,-X,+Y,-Y,+Z,-Z). When the file's pixel format has a
// direct DXGI equivalent the pixels are not copied: subresource offsets are relative to the buffer passed in, which
// must outlive createTexture(). When a conversion is needed, the converted pixels are in DdsImage::bytes and the
// offsets are relative to bytes. DdsImage::pixels() hides the difference.
//
// Formats (legacy header; DX10 header: any DXGI format in the table of dds.cpp):
//   FourCC DXT1 -> BC1, DXT2/DXT3 -> BC2, DXT4/DXT5 -> BC3 (DXT2/DXT4 premultiplied alpha is passed as is),
//   ATI1/BC4U -> BC4_UNORM, BC4S -> BC4_SNORM, ATI2/BC5U -> BC5_UNORM, BC5S -> BC5_SNORM,
//   numeric D3DFMT 36 A16B16G16R16 -> R16G16B16A16_UNORM, 110 Q16W16V16U16 -> R16G16B16A16_SNORM,
//   111 R16F, 112 G16R16F, 113 A16B16G16R16F, 114 R32F, 115 G32R32F, 116 A32B32G32R32F -> the matching FLOAT formats,
//   masks: A8R8G8B8 -> B8G8R8A8_UNORM, X8R8G8B8 -> B8G8R8X8_UNORM, A8B8G8R8 -> R8G8B8A8_UNORM,
//          G16R16 -> R16G16_UNORM, A2B10G10R10 -> R10G10B10A2_UNORM, L8 -> R8_UNORM, L16 -> R16_UNORM,
//          A8L8 -> R8G8_UNORM, A8 -> A8_UNORM (all direct, no copy);
//   converted to B8G8R8A8_UNORM: R8G8B8 (24-bit), X8B8G8R8 (alpha forced to 255), R5G6B5, X1R5G5B5, A1R5G5B5,
//          A4R4G4B4, X4R4G4B4, A8R3G3B2, R3G3B2, A4L4 (the 16-bit D3D11 formats are optional on a device).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include <dxgiformat.h>
#include "c5r/status.h"

namespace c5r { namespace gamefs {

struct DdsSubresource {
    size_t offset = 0;      // into DdsImage::pixels()
    size_t size = 0;        // bytes of this subresource (slicePitch * depth)
    uint32_t rowPitch = 0;  // bytes per row (per row of 4x4 blocks for BC formats)
    uint32_t slicePitch = 0;
    uint32_t width = 0, height = 0, depth = 0;
};

enum class DdsDim { Tex1D, Tex2D, Tex3D };

struct DdsImage {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0, height = 0, depth = 1, mipLevels = 1, arraySize = 1;  // arraySize counts cube faces
    bool cube = false;
    DdsDim dim = DdsDim::Tex2D;
    std::vector<DdsSubresource> subresources;  // D3D11 order: array slice major, mip minor
    std::vector<uint8_t> bytes;                // converted pixel data when a conversion was needed
    const uint8_t* src = nullptr;              // the buffer given to parseDds (used when bytes is empty)
    std::string sourceFormat;                  // e.g. "DXT5", "D3DFMT_113", "A8R8G8B8", "DX10"
    bool converted = false;
    const uint8_t* pixels() const { return converted ? bytes.data() : src; }
};

// D3D11 limits used by the dimension checks.
const uint32_t kMaxTex2D = 16384, kMaxTex3D = 2048, kMaxTex1D = 16384, kMaxArray = 2048;

// Checks (Status code in brackets):
//   size < 4 [Truncated]; magic != "DDS " [BadMagic]; size < 128 [Truncated]; header size != 124 [BadLayout];
//   pixel-format size != 32 [BadLayout]; DX10 FourCC with fewer than 20 more bytes [Truncated];
//   width, height or (volume) depth == 0 [BadLayout]; DX10 array size 0 [BadLayout];
//   unknown FourCC, mask set or DXGI format [Unsupported];
//   a cube map without all six faces [Unsupported] (D3D11 has no partial cubes); a volume cube [BadLayout];
//   dimensions above the D3D11 limits [Unsupported];
//   mip count above floor(log2(max dimension)) + 1 [BadLayout];
//   a BC format whose top level is not a multiple of 4 in width and height [Unsupported] (D3D11 cannot create it);
//   pixel data shorter than the subresources need [Truncated]. Trailing bytes after the last subresource are allowed.
Status parseDds(const uint8_t* data, size_t size, DdsImage& out);

// Creates an IMMUTABLE texture (Texture1D/2D/3D; cube maps as a 2D array with TEXTURECUBE) and, if srv is not null,
// a shader-resource view of the whole resource (TEXTURE2D, TEXTURE2DARRAY, TEXTURECUBE, TEXTURECUBEARRAY, TEXTURE3D,
// TEXTURE1D, TEXTURE1DARRAY). Device: the device's format-support query fails or a create call fails, with the HRESULT.
Status createTexture(ID3D11Device* dev, const DdsImage& img, ID3D11Resource** tex, ID3D11ShaderResourceView** srv);

// Short DXGI format name ("BC3_UNORM", ...) for reports; "DXGI_<n>" if not in the table.
std::string dxgiFormatName(DXGI_FORMAT f);
// Bits per pixel for uncompressed formats, or bytes per 4x4 block (8/16) with *isBlock = true; 0 if unknown.
uint32_t formatBits(DXGI_FORMAT f, bool* isBlock);

}} // namespace c5r::gamefs
