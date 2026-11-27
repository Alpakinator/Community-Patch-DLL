// c5r/terrainsets/lean.h - the EXE's `.lean` bump textures (LEAN mapping data) that WaterShader binds at t0-t3
// (issue 3602, epic 3600; research/05-graphics-composition/water-system.md section 8). Own code, written from the
// knowledge base's description of the format; the files are read from the player's install at run time through
// c5r::gamefs (D83: nothing of them is committed).
//
// Format [C: the reader 0x8F3820 and Water::Startup 0x5F06A0 decompiled; sizes checked on all six files of the
// install, 2026-11-13], little-endian:
//   0x00 char[4] "LEAN"                0x04 f32 (read into the loader's record, not used by Water::Startup)
//   0x08 u32 width   0x0C u32 height    0x10 u32 mip count   0x14 u32 bytes per channel (1 or 2)
//   0x18 u32 offset of the four-channel block (0x24 in every file)   0x1C u32 offset of the one-channel block
//   0x20 u8 (read into the record, not used)   0x21..0x23 padding (uninitialised bytes in the files)
// Each block holds the whole mip chain, largest first, mip m = max(1, width >> m) x max(1, height >> m) texels, rows
// tightly packed: the four-channel block at 4 * bytesPerChannel per texel, the one-channel block at bytesPerChannel.
// Water::Startup creates one texture from each block with these data as initial data: DXGI_FORMAT_R16G16B16A16_UNORM
// (format word 0xb) and DXGI_FORMAT_R16_UNORM (0x38), full mip chain, for the 16-bit files it loads [C]. The 8-bit
// variant (ice_bump*.lean) is not loaded by the water; its formats here (R8G8B8A8_UNORM / R8_UNORM) are [I].
//
// Which file goes where [C, Water::Startup and the draw objects of 0x739AE0 / 0x6B1580]:
//   ocean passes:  t0 LEAN1_1 = waterbumps16.lean four-channel (Water+0x1C), t1 LEAN1_2 = its one-channel (Water+0x20);
//                  t2, t3 unbound.
//   river passes:  t0 / t1 = riversmooth16.lean (Water+0x24 / +0x2C), t2 / t3 = riverrough_new16.lean (+0x28 / +0x30);
//                  loaded only when the water-quality global [0xD90F68] != 0.
//   sampler state 0 (linear, wrap) on all four.
//
// parseLean() checks:  size < 0x24 [Truncated]; magic [BadMagic]; width or height 0 or above 16384 [BadLayout];
// mip count 0 or above the full chain of width x height [BadLayout]; bytes per channel not 1 or 2 [Unsupported];
// an offset inside the header, the blocks overlapping, or the four-channel block not first [BadLayout]; a block
// running past the end of the data [Truncated].
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"

namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace terrainsets {

const uint32_t kLeanHeaderSize = 0x24;
const uint32_t kLeanMaxSize = 16384;

struct LeanFile {
    float unknown04 = 0;          // +0x04 (0.33 waterbumps16, 0.7 riverrough16, 2.5 riverrough_new16) [I meaning]
    uint32_t width = 0, height = 0, mips = 0, bytesPerChannel = 0;
    uint32_t offFour = 0, offOne = 0;
    uint8_t byte20 = 0;           // +0x20 (1 in every file)
    const uint8_t* four = nullptr;   // the four-channel block, inside the caller's bytes
    size_t fourBytes = 0;
    const uint8_t* one = nullptr;    // the one-channel block
    size_t oneBytes = 0;
    DXGI_FORMAT fourFormat = DXGI_FORMAT_UNKNOWN, oneFormat = DXGI_FORMAT_UNKNOWN;
};

// Texels of a full or partial mip chain (sum over m < mips of max(1, w >> m) * max(1, h >> m)).
uint64_t leanTexels(uint32_t width, uint32_t height, uint32_t mips);
// Number of mips of a full chain of width x height.
uint32_t leanFullChain(uint32_t width, uint32_t height);

// Parses the header and locates the blocks; `out` points into p, which must outlive it.
Status parseLean(const uint8_t* p, size_t n, LeanFile& out);

// One mip of one block: data pointer, size in texels and row pitch in bytes. BadLayout if mip >= mips.
struct LeanMip {
    const uint8_t* data = nullptr;
    uint32_t width = 0, height = 0, rowPitch = 0;
};
Status leanMip(const LeanFile& f, bool fourChannel, uint32_t mip, LeanMip& out);

// The two textures as Water::Startup creates them (immutable, shader resource, every mip from the file).
// Errors: BadLayout (dev or an out pointer null), Device.
Status createLeanTextures(ID3D11Device* dev, const LeanFile& f, ID3D11Texture2D** four, ID3D11Texture2D** one);

// The water's .lean files by role, as Water::Startup names them.
const char* const kLeanOcean = "waterbumps16.lean";
const char* const kLeanRiver1 = "riversmooth16.lean";      // river t0 / t1
const char* const kLeanRiver2 = "riverrough_new16.lean";   // river t2 / t3

// Reads `name` through the game's file system and parses it; `bytes` keeps the file (out points into it).
// Errors: those of GameFiles::read (NotFound when the name does not resolve) and of parseLean.
Status loadLean(const gamefs::GameFiles& fs, const std::string& name, std::vector<uint8_t>& bytes, LeanFile& out);

}} // namespace c5r::terrainsets
