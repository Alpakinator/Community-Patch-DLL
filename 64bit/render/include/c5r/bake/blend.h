// c5r::bake blend - the first pass of the terrain bake (issue 3402, epic 3400): the per-vertex layer weights of the
// 67 x 67 grid and the blend draw into three 512 x 512 R16G16B16A16_FLOAT targets with the EXE's own shaders.
//
// Re-implemented from the description in research/05-graphics-composition/terrain-bake-blend.md (D83: no decompiler
// output, no EXE bytes and no game data in this repository; the shaders are read from the player's EXE at run time and
// the textures from the player's install). Arithmetic follows the EXE's precision step by step (IEEE single, one
// double-precision step for the position x, truncation to bytes); it is checked byte for byte against captured vertex
// buffers in render/tests/bakeblend.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "c5r/bake/bake_types.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r {
namespace bake {

// ---------------------------------------------------------------- the CPU side (0x5118E0 jobs -> 0x5CE920)

// One tap of the weight kernel (TR8+0x000 dx, +0x144 dy, +0x288 weight; count +0x3CC).
struct KernelTap {
    int32_t dx = 0, dy = 0;
    float w = 0;
};
const uint32_t kMaxKernelTaps = 81;  // the EXE reserves 81 entries per table

// The kernel the EXE builds at start-up (0x6BB2B0): 5 x 5 separable, row factors (0.125, 0.636056, 1, 0.636056, 0.125),
// normalised by its sum. 25 taps, dy outer, dx inner.
std::vector<KernelTap> defaultKernel();

// The inputs of the vertex fill that are global to the map.
struct BlendInputs {
    const heightfield::HeightGrid* grid = nullptr;  // heights h and material bytes mat (mat must be present)
    const LayerTables* tables = nullptr;            // layerCount, firstLayerOfMaterial, slopeProjected
    std::vector<KernelTap> kernel = defaultKernel();
    float stepX = 1.0f, stepY = 0.866f, stepZ = 0.2f;  // TR8+0x1AC4 / +0x1AC8 / +0x1ACC (= the grid's sx, sy, sz)
};

// Where a tile is: origin in height samples (tile+0xDC/+0xE0) and its extent (tile+0xD4/+0xD8: 65, 64 in the last
// column/row). The grid has (extentX + 2) x (extentY + 2) vertices.
struct TileGeom {
    int32_t originX = 0, originY = 0;
    uint32_t extentX = kTileSamples + 1, extentY = kTileSamples + 1;
};

// Slope classes of the grid's vertices ((extentX+2) * (extentY+2) bytes, row-major, values 0, 1, 2), or empty for
// "all 0". The class comes from the height field's normal (0x728240); its filter is not ported yet, so callers pass the
// classes they have, or empty. They change the result only where a slope-projected material is within reach of the
// kernel. [STAND-IN until the normal filter of 0x728240 / 0x747580 is ported]
typedef std::vector<uint8_t> SlopeClasses;
// The class rule itself, for a normal n: nz > 0.9 -> 0; else 1 when |ny| > |nx|, else 2.
uint8_t slopeClass(float nx, float ny, float nz);

// The normalised weights of all layers (layerCount entries) at height sample (sx, sy) for a vertex of slope class
// cls (0x527720). Checks: grid without material bytes, layerCount 0 or > 256, a material whose first layer + class is
// >= layerCount [BadLayout]; empty kernel or more than kMaxKernelTaps taps [BadLayout].
Status layerWeights(const BlendInputs& in, int32_t sx, int32_t sy, uint8_t cls, std::vector<float>& out);

// The 48-byte vertices of a tile's blend grid. Checks: those of layerWeights; null grid/tables [BadLayout]; extents
// 0 or above 65 [BadLayout]; list count > 8 or an id >= layerCount [BadLayout]; slope classes neither empty nor of
// the grid's size, or a class > 2 [BadLayout].
Status fillBlendVertices(const BlendInputs& in, const TileGeom& tile, const LayerList& list, const SlopeClasses& cls,
                         std::vector<BlendVertex>& out);

// The 16-bit indices of the grid (0x5ABE70), relative to the tile's first vertex: bands of 6 quad columns, rows
// bottom-up in each band, two triangles per quad, the diagonal alternating with the column. (gw-1)*(gh-1)*6 indices
// for a gw x gh vertex grid. Checks: gw or gh < 2, or gw*gh > 65536 [BadLayout].
Status buildBlendIndices(uint32_t gridW, uint32_t gridH, std::vector<uint16_t>& out);

// ---------------------------------------------------------------- the constant buffers

const uint32_t kTerrainLayersFloat4 = 96;  // shared buffer id 18, 0x600 bytes (cb2 of the blend shaders)
const uint32_t kOwnBlockBytes = 0x500;      // the material's slot-0 buffer (the program's block is 0x4C0)

// TerrainLayers (0x6C47F0): for layer l < layerCount, x = the set's texture scale quantised to whole repeats over the
// map (X = float(mapPlotsX * 64 - 1)), y = projection. Entries >= layerCount are 0. Checks: tables inconsistent
// (layerCount > 96, setOfLayer / projectionOfLayer shorter than layerCount, a set index >= sets.size()) [BadLayout];
// mapPlotsX 0 [BadLayout].
Status fillTerrainLayers(const LayerTables& t, uint32_t mapPlotsX, std::vector<float>& out /* 384 floats */);

// The program's own block as 0x694A60 writes it for this tile, as far as it is known: [0].xy = (-1, +1) / (2 *
// largestCacheSize) (the only part pass 2 reads), [1..count] = (scale as in TerrainLayers, projection, 0, 0) per slot,
// [73].xy = float(tile+0xE4), float(tile+0xE8). Everything else is left 0 (the EXE leaves earlier tiles' values there).
// Checks: those of fillTerrainLayers; largestCacheSize 0 [BadLayout]; list invalid [BadLayout].
Status fillOwnBlock(const LayerTables& t, uint32_t mapPlotsX, const LayerList& list, uint32_t largestCacheSize,
                    int32_t tileE4, int32_t tileE8, std::vector<uint8_t>& out /* kOwnBlockBytes */);

// ---------------------------------------------------------------- the GPU pass

// The EXE's blend shaders: TerrainShaderScreen (material 76) pass 2 (0xD90F28 != 0, 0xD90F24 == 0), SM5.
const uint32_t kBlendVsVa = 0x067D9550;
const uint32_t kBlendPsVa = 0x067F1E40;

// Per-slot textures of one draw: t i = diffuse, t 8+i = height map, t 16+i = spec map (null = the dummy texture).
struct BlendTextures {
    ID3D11ShaderResourceView* srv[24] = {};
};

// The three targets after the draw (mip 0 read back), R16G16B16A16_FLOAT, 512 x 512, row-major, 4 halves per texel.
struct BlendTargets {
    uint32_t size = 0;
    std::vector<uint16_t> texels[3];
};

class BlendPass {
public:
    BlendPass();
    ~BlendPass();
    BlendPass(const BlendPass&) = delete;
    BlendPass& operator=(const BlendPass&) = delete;

    // Creates the shaders from the EXE's .shader section and checks their reflection (VS: the 7 inputs of the 48-byte
    // vertex, cb0 >= 1 and cb2 = 96 registers; PS: t0..t23, s0..s2, three outputs, no constant buffer). Then the input
    // layout, the states as captured (blend ONE+ONE add with IndependentBlendEnable off, so on all three targets; depth
    // off; cull none, solid; sampler MIN_MAG_MIP_LINEAR wrap x 3), the three targets (512^2, 10 mips, GENERATE_MIPS), the
    // 32 x 32 zero RGBA8 dummy texture and the buffers. Checks: no device [BadLayout]; a blob missing [NotFound]; a blob of
    // the wrong stage or a reflection that differs [BadLayout]; D3D11 failures [Device].
    Status init(ID3D11Device* dev, const shaders::ShaderSection& exe);
    // Same, with the two blob addresses given (for the negative tests).
    Status init(ID3D11Device* dev, const shaders::ShaderSection& exe, uint32_t vsVa, uint32_t psVa);

    // Clears the targets to 0, draws the grid once (one indexed draw), generates the mips. Checks: not initialised,
    // vertices not (w+2)*(h+2) for the index count, indices referencing past the vertices, constant blocks of the wrong
    // size [BadLayout]; D3D11 failures [Device].
    Status run(ID3D11DeviceContext* ctx, const std::vector<BlendVertex>& vertices, const std::vector<uint16_t>& indices,
               const std::vector<uint8_t>& ownBlock, const std::vector<float>& terrainLayers, const BlendTextures& tex);
    // Reads mip 0 of the three targets back. Device on failure; BadLayout if not initialised.
    Status readTargets(ID3D11DeviceContext* ctx, BlendTargets& out);
    // The targets themselves (for the passes after the blend, issue 3403).
    ID3D11Texture2D* target(int i) const;
    ID3D11ShaderResourceView* targetSrv(int i) const;
    ID3D11ShaderResourceView* dummySrv() const;

private:
    struct Impl;
    Impl* p_ = nullptr;
};

// ---------------------------------------------------------------- textures by name

// TerrainDef.csv is read by issue 3401's parseTerrainDef / loadTerrainDef (c5r/bake/layers.h), the one parser since
// issue 3404 (3402's local stand-in parseTerrainDefNames was removed).

// Loads the textures of one draw by the CSV's names through c5r::gamefs: for slot i < list.count with k =
// tables.setOfLayer[id], t i = sets[k].diffuse, t 8+i = sets[k].heightmap, t 16+i = sets[k].specmap or the dummy when
// empty or "NULL"; unused slots the dummy. The SRVs are owned by the cache (released with it).
// A name the install does not have (issue 3404; terrain-bake-comparison.md 9): the EXE's loader leaves the set's texture
// handle 0 (live: TR8+0x908[8] = 0 for Generic_Gibraltar_H.dds, which no file of the install holds), the blend's draw
// object takes the handle as it is (0x69556D) and the binder takes view [0x414C348 + 12 * handle] without a check
// (0x8BD1F0) [C]; that entry 0 holds no view, which samples as 0 like the all-zero dummy, is [I]. So by default
// (MissingTexture::AsHandle0) the slot gets the dummy and the name is recorded in missing(); MissingTexture::Fail keeps
// the old behaviour. Checks: list invalid [BadLayout]; with Fail a name the install does not have [NotFound]; a file
// that exists but does not parse or create stays an error (gamefs's codes) under either rule.
enum class MissingTexture { AsHandle0, Fail };
class BlendTextureCache {
public:
    BlendTextureCache();
    ~BlendTextureCache();
    BlendTextureCache(const BlendTextureCache&) = delete;
    BlendTextureCache& operator=(const BlendTextureCache&) = delete;
    Status bind(ID3D11Device* dev, const gamefs::GameFiles& fs, const LayerTables& t, const LayerList& list,
                ID3D11ShaderResourceView* dummy, BlendTextures& out);
    size_t loaded() const;
    void setMissingRule(MissingTexture rule);
    // The names bound as handle 0 so far (lower case as looked up, each once).
    const std::vector<std::string>& missing() const;
    // Issue 0417: every file load and missing name so far, in order (a bake's first uses name the files they read).
    struct Load {
        std::string name;
        uint64_t bytes = 0;
        double readMs = 0;     // gamefs exists + read
        double createMs = 0;   // parse + CreateTexture2D + view
        bool missing = false;  // bound as handle 0
    };
    const std::vector<Load>& loads() const;
    // Ticket 0698: textures made elsewhere (the pipeline's loader thread, terrain-bake-slow-first-use.md section 8).
    // has: the name is in the cache, or recorded as missing under MissingTexture::AsHandle0 (bind reads nothing for it).
    bool has(const std::string& name) const;
    // provide: the cache takes srv for name (it AddRefs; the caller keeps its own reference) unless the name is in
    // already; returns whether it took it. Not recorded in loads(). Null srv: false.
    bool provide(const std::string& name, ID3D11ShaderResourceView* srv);
    // provideMissing: the install lacks the name: recorded as bind records it (missing(), lower case, once) when the rule
    // is AsHandle0, and true; under MissingTexture::Fail false and nothing recorded (a bind then fails NotFound as before).
    bool provideMissing(const std::string& name);

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}  // namespace bake
}  // namespace c5r
