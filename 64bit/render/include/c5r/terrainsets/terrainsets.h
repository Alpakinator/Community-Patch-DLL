// c5r/terrainsets/terrainsets.h - the terrain-side shader sets of CivilizationV_DX11.exe (task 2903, epic 2900): which
// blobs of the `.shader` section draw terrain, the GPU terrain bake, water and rivers, terrain decals, the overlay
// (borders) and the fog of war; their constant-buffer layouts with member names and offsets, their texture and sampler
// slots and what the EXE binds to each. Research: research/05-graphics-composition/asset-pipeline.md section 3.
//
// How the EXE drives a set [C, build 4390913]: every shader set is one entry of the renderer's material table
// (0x5d1e5c0 + material * 0x520: up to 64 passes of {VS, PS, GS, HS, DS} handle words, then at +0x508 the D3D buffer of
// slot 0 and at +0x510 its size). A "program" object (one per material, 97 constructors calling 0x8c6a50) binds the
// constant buffers: slot 0 = the program's own buffer (copied from a CPU block at draw time), slots 1.. = shared
// buffers of the renderer (0x8bd0a0(id, size) at init; objects at 0x4f1dd88 + id * 0x1c), the same buffers for VS, PS,
// HS and DS. Textures and samplers come from the draw object (+0x18 texture handles, +0x4c sampler-state ids, +0x7c
// count; VS: +0x6c, +0x74, +0x7d), sampler state i = the i-th of the 13 states created by 0x8be700.
//
// Confidence marks: Conf::Confirmed [C] = read in the decompile or the disassembly; Live [L] = seen in a running game;
// Inferred [I] = named from use, not proven. A member's `source` says where the EXE takes the value from.
//
// validateSet() checks a description against the parsed blobs (negative tests in tests/terrainsets/):
//   a blob VA that is not in the section                                            NotFound
//   a blob of another stage than the pass slot it is named in                        BadLayout
//   a blob whose create-site handle differs from the pass's handle (sites applied)   BadLayout
//   a constant buffer the blob declares at a slot the set has no layout for          BadLayout
//   a declared buffer larger than the layout (vec4 count * 16 > size)                BadLayout
//   a register component the code reads that no member covers                       BadLayout
//   a dynamically indexed read whose base register is not inside an array member    BadLayout
//   a texture the blob declares at a slot the set does not describe, or of another dimension   BadLayout
//   a sampler the blob declares at a slot without a texture slot of the same index   BadLayout
//   a layout whose members overlap, run past its size, straddle a 16-byte register, have count 0,
//   or whose size is not a multiple of 16                                            BadLayout (validateLayout)
#pragma once
#include <cstdint>
#include <vector>
#include "c5r/status.h"
#include "c5r/shaders/dxbc.h"

namespace c5r { namespace shaders { class ShaderSection; struct ShaderBlob; } }

namespace c5r { namespace terrainsets {

enum class Conf : uint8_t { Confirmed, Live, Inferred };
const char* confTag(Conf c); // "C", "L", "I"

enum class MemberType : uint8_t { Float, Float2, Float3, Float4, Float4x4 };
uint32_t memberTypeSize(MemberType t); // 4, 8, 12, 16, 64 bytes per element
const char* memberTypeName(MemberType t);

struct CbMember {
    const char* name;
    uint32_t offset;      // bytes from the start of the buffer
    MemberType type;
    uint32_t count;       // elements; arrays (count > 1) have a 16-byte stride (Float4 / Float4x4 elements only)
    const char* source;   // what the EXE writes there and from where
    Conf conf;
};

struct CbLayout {
    const char* name;
    int sharedId;         // id of the renderer's shared buffer (0x8bd0a0), -1 for a program's own buffer (slot 0)
    uint32_t sizeBytes;   // size of the D3D buffer the EXE creates
    const char* filledBy; // EXE function that writes the CPU block, "" if not traced
    std::vector<CbMember> members;
};

struct CbBinding {
    uint32_t slot;          // b#, same for every stage of the set
    const CbLayout* layout;
};

enum class TexDim : uint8_t { Tex2D = 3, Tex3D = 5, Cube = 6 }; // D3D10_SB_RESOURCE_DIMENSION
const char* texDimName(TexDim d);

struct TexSlot {
    uint32_t slot;        // t#; the sampler the shader pairs with it is s# of the same index
    TexDim dim;
    const char* name;     // the EXE's own slot name (generated shader definitions, .data 0xc9e718) or a descriptive one
    const char* bound;    // what the EXE binds there
    int samplerState;     // the renderer sampler state the draw object pairs with it (samplerState()), -1 if not traced
    Conf conf;
};

struct PassDesc {
    uint32_t index;       // pass index inside the material
    uint32_t handle[4];   // VS, PS, HS, DS handle globals (16-bit words the EXE stores the created shader in), 0 = none
    uint32_t sm50[4];     // VS, PS, HS, DS blob VAs at shader level 10 (set 5_0), 0 = none
    uint32_t sm41[2];     // VS, PS at level 9 (set 4_1), 0 = the pass has none
    uint32_t sm40[2];     // VS, PS at levels 7 and 8 (set 4_0)
    const char* use;      // which draw selects the pass
};

struct ShaderSetDesc {
    const char* name;     // the material's own name (string table 0xa96334..)
    uint32_t material;    // material index
    const char* role;     // terrain, terrain-bake, water, river, decal, overlay, fow, ...
    const char* drawnBy;  // EXE system and functions that draw with it
    std::vector<PassDesc> passes;
    std::vector<CbBinding> cbuffers;
    std::vector<TexSlot> psTextures;
    std::vector<TexSlot> vsTextures;
    const char* vertexInput; // input layout or vertex-ID scheme
    const char* notes;       // render state, open points
    Conf identification;     // how sure the set -> system identification is
};

const std::vector<ShaderSetDesc>& allSets();
const ShaderSetDesc* findSet(const char* name);

struct SamplerStateDesc {
    uint32_t id;
    uint32_t filter;      // D3D11_FILTER
    uint32_t address[3];  // D3D11_TEXTURE_ADDRESS_MODE U, V, W
    uint32_t maxAnisotropy;
    uint32_t comparison;  // D3D11_COMPARISON_FUNC
    bool srgbView;        // renderer+0x30568 + id*8: the draw takes the texture's second view [I: the sRGB view]
    Conf conf;
};
const std::vector<SamplerStateDesc>& samplerStates();
const SamplerStateDesc* samplerState(uint32_t id);

// Which shader level's blob of a pass to check.
enum class Level : uint8_t { SM50, SM41, SM40 };

Status validateLayout(const CbLayout& l);
// Checks one blob against the set: `stage` is the pass slot it is named in, `handle` the pass's handle for that stage;
// checkHandle compares it with the blob's create-site handle (pass sec.createSitesApplied()).
Status validateBlob(const ShaderSetDesc& set, const shaders::ShaderBlob& blob, shaders::Stage stage, uint32_t handle,
                    bool checkHandle);
// Every layout of the set, then every blob of every pass at every level that has one.
Status validateSet(const ShaderSetDesc& set, const shaders::ShaderSection& sec);

}} // namespace c5r::terrainsets
