// c5r/bake/post.h - the terrain bake's passes after the blend, as far as the BC1 caches t0 and t8 (issue 3403, epic 3400).
//
// Behaviour (research/05-graphics-composition/terrain-bake-passes.md; D83: written from that description, no decompiler
// output): on one D3D11 device, with the EXE's own SM5 shaders read from CivilizationV_DX11.exe at run time,
//   1. GenerateMips on the three 512 x 512 R16G16B16A16_FLOAT blend targets (albedo, normal, spec temps), unless the
//      caller says their mips are ready;
//   2. "Normal Calculation": NormalCalculate over the normal temp's red channel into a 512 RGBA8 target, mips;
//   3. "Macro Shadow": TerrainShaderScreen pass 3, `shadowDraws` (3) additive draws over the tile's height page into a
//      512 RGBA8 target cleared to 0, mips;
//   4. "Terrain Cache": CacheBake (pass 1 with two targets when `needSecond`, else pass 0) into RGBA8 targets of the size
//      class, mips; with `nearCache` given (issue 0332, the EXE's state-5 bake) first into the near pair at its own size,
//      GenerateMips on it;
//   5. "DXT Compress": CompressBC per mip into R32G32_UINT block targets of size/4, then CopyResource into BC1_TYPELESS
//      textures of the size class: t0 (colour) and, when `needSecond`, t8 (baked specular).
// States, samplers, grids and constants follow the capture of 3023 run 3 (terrain-bake-passes.md sections 2 and 3).
// Decals (epic 0166, issue 4104; terrain-decals.md sections 4 and 11): with `decals` and `decalPass` given, the tile's
// pass-0 decals are drawn into mip 0 of the three blend targets before step 1 (the mips are then always generated,
// whatever `blendMipsReady` says) and its pass-1 decals (tree shadows) into the macro-shadow target after step 3's
// draws, before its mips. Without decals (`decals` null or empty) every step is the same as before, bit for bit.
//
// Errors: init without a device or with a ShaderSection that lacks a blob: BadLayout / NotFound; a blob of another stage
// or whose declarations differ from what the pass binds (reflection check): BadLayout; D3D11 failures: Device.
// run before init: NotFound; a missing or wrong input (blend target format, size, mip count or bind flags, missing
// height page, cube or LUT, normals count, size class not 64/128/256/512, decals without a decal pass, a near cache of
// the wrong kind, a near t8 missing / given against needSecond, near caches of two sizes or one texture twice): BadLayout;
// the decal pass's own errors (c5r/decal/decal.h) as it returns them.
// A failed call leaves the outputs null.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/bake/bake_types.h"

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }
namespace c5r { namespace decal { class DecalPass; struct TileDecalList; } }

namespace c5r { namespace bake {

// The SM5 (set 5_0, shader level 10) blobs of the passes, by VA in the EXE's .shader section (build 4390913).
struct PostShaderVas {
    uint32_t normalVs = 0x067d8760, normalPs = 0x067eab30;      // NormalCalculate pass 0
    uint32_t shadowVs = 0x067e4590, shadowPs = 0x067f6698;      // TerrainShaderScreen pass 3 ("Macro Shadow")
    uint32_t cacheVs = 0x067d8cb8;                              // CacheBake
    uint32_t cachePs1 = 0x067ebe78, cachePs2 = 0x067ed018;      // CacheBake pass 0 (one target) / pass 1 (two)
    uint32_t compressVs = 0x06743180, compressPs = 0x067434f0;  // CompressBC pass 0
};

// Mip count of the caches for a size class (tile+0x17C): 64 -> 4, 128 -> 5, 256 -> 6, 512 -> 7; 0 for anything else.
uint32_t cacheMips(uint32_t sizeClass);

// ---- The near-camera pair (issue 0332; terrain-bake-passes.md 3.5.1 and 3.5.3): tile+0x190 / +0x194, R8G8B8A8_UNORM,
// the full chain down to 1 x 1, made by the EXE's state-5 bake (CacheBake at that size, then GenerateMips), no compression.
// Mip count of one near cache: 64 -> 7, 128 -> 8, 256 -> 9, 512 -> 10; 0 for anything else.
uint32_t nearCacheMips(uint32_t size);
// Bytes of one near cache with its whole chain (4 bytes per texel): 256 -> 349,524, 512 -> 1,398,100; 0 for another size.
uint64_t nearCacheBytes(uint32_t size);
// One near cache as the pools hold it: R8G8B8A8_UNORM, size^2, nearCacheMips(size) mips, RENDER_TARGET | SHADER_RESOURCE,
// GENERATE_MIPS, default usage. Errors: no device or a size that is not 64/128/256/512 BadLayout; D3D11 Device /
// OutOfMemory. On error *out stays null.
Status createNearCache(ID3D11Device* dev, uint32_t size, ID3D11Texture2D** out);

// One vertex of the CacheBake grid (24 bytes, the EXE's layout): POSITION f32x3 (not read by the shader), NORMAL f16x4
// (unit terrain normal, w = 1), TEXCOORD0 f16x2 = (i/66, j/66).
#pragma pack(push, 1)
struct CacheVertex {
    float pos[3];
    uint16_t normal[4];
    uint16_t uv[2];
};
#pragma pack(pop)
static_assert(sizeof(CacheVertex) == 24, "CacheVertex must match the EXE's 24-byte vertex");

// Grid builders (exposed for the tests; run() uses them). kBlendGrid = 67 vertices per side, 66 x 66 quads.
// Index order: strips of 6 quad columns, each strip row by row; quad (i, j) with a = j*67 + i, b = a+1, c = a+67,
// d = a+68 is (d, b, c), (c, b, a) for even i and (d, b, a), (c, d, a) for odd i. 26,136 indices.
void buildGridIndices(uint16_t out[66 * 66 * 6]);
// The macro-shadow grid in the blend vertex format: POSITION = ((2i - 66) / 66, 1 - j / 66 * 2, 0.5), TEXCOORD0 =
// (originX - 1 + i, originY - 1 + j) in height samples (tile+0xDC/+0xE0); other fields 0.
void buildShadowGrid(int originX, int originY, BlendVertex out[67 * 67]);
// The CacheBake grid from 67 x 67 normals (x, y, z each; row-major, j outer); sizeClass sets the half-texel margin of
// POSITION (unused by the shader).
void buildCacheGrid(const float* normals, uint32_t sizeClass, CacheVertex out[67 * 67]);
// The same for a tile of extent (ex, ey): gw = ex + 2 by gh = ey + 2 vertices (66 on the short side of the tiles of the
// last column / row; issue 3404), steps 1/(gw-1) and 1/(gh-1) where the 67 grid has 1/66; (gw-1)*(gh-1)*6 indices.
// gw and gh must be 2..67 (the callers check; run() fails with BadLayout).
void buildGridIndices(uint32_t gw, uint32_t gh, std::vector<uint16_t>& out);
void buildShadowGrid(int originX, int originY, uint32_t gw, uint32_t gh, std::vector<BlendVertex>& out);
void buildCacheGrid(const float* normals, uint32_t sizeClass, uint32_t gw, uint32_t gh, std::vector<CacheVertex>& out);

// Values the EXE takes from TerrainFrame, its own buffers and globals (terrain-bake-passes.md section 5).
struct PostConstants {
    float halfVector[3] = {0.0f, -0.21644f, 0.976296f};  // TerrainFrame reg 21 = normalize(LightDir + CameraAxisZ)
    float shadowDir[3] = {0.433013f, -0.25f, 0.866025f}; // TerrainFrame reg 17 (AuxDir): the macro shadow's direction
    float hexScale[3] = {1.0f, 0.866f, 0.2f};            // TerrainFrame reg 16 (.z = height unit)
    float heightTexelScale[2] = {1.0f / 1024, -1.0f / 1024}; // TerrainFrame reg 22
    float heightOffset[2] = {0, 0};                      // own reg 73.xy = tile+0xE4/+0xE8 as float
    float normalStep[2] = {1.0f / 512, 1.0f / 512};      // Normal Calculation reg 0
    float normalStrength = 16.0f;                        // Normal Calculation reg 1.x (0x9EFBA8)
    uint32_t shadowDraws = 3;                            // 0xD90F6C at quality 3; weight 1/shadowDraws each
};

struct PostInputs {
    // albedo, normal, spec temps: 512 x 512 R16G16B16A16_FLOAT (or _TYPELESS), 10 mips, bind RENDER_TARGET |
    // SHADER_RESOURCE, misc GENERATE_MIPS; mip 0 holds the blend.
    ID3D11Texture2D* blend[3] = {nullptr, nullptr, nullptr};
    bool blendMipsReady = false;            // true: mips 1..9 already generated (by the blend pass)
    ID3D11ShaderResourceView* heightPage = nullptr;  // tile+0xEC: 2D, height / 255 in the alpha channel (A8_UNORM)
    int originX = 0, originY = 0;           // tile+0xDC / +0xE0 (height samples)
    const float* gridNormals = nullptr;     // gridW x gridH x 3 floats (terrain-bake-passes.md 5.1)
    uint32_t gridNormalCount = 0;           // must be gridW * gridH
    uint32_t gridW = 67, gridH = 67;        // the tile's grid: extent + 2 (66 for the last column / row), 2..67
    ID3D11ShaderResourceView* sunCube = nullptr;      // Sunlight_<Style>_Terrain (cube)
    ID3D11ShaderResourceView* shadowCube = nullptr;   // Shadowlight_<Style>_Terrain (cube)
    ID3D11ShaderResourceView* colorKey = nullptr;     // ColorKey Terrain LUT of the style (3D)
    uint32_t sizeClass = kCacheSizeDefault; // tile+0x17C: 64 / 128 / 256 / 512
    bool needSecond = true;                 // tile+0x2FC: make t8
    // The tile's decals in draw order (c5r::decal::decalsForTile) and the pass that draws them (epic 0166, 4104). Both
    // or neither: a non-empty list without a pass fails with BadLayout. The pass must live on the same device.
    const decal::TileDecalList* decals = nullptr;
    decal::DecalPass* decalPass = nullptr;
    // The near pair (issue 0332): the EXE's state-5 bake. When nearCache[0] is given, CacheBake (the same program, grid and
    // inputs) is drawn also straight into nearCache[0] (and nearCache[1] when needSecond) with a viewport of their size, and
    // GenerateMips makes their chains; the BC1 path at sizeClass follows unchanged (its outputs are the same, bit for
    // bit, as without the near pair). Each target: createNearCache's kind (R8G8B8A8_UNORM or _TYPELESS, square 64 / 128
    // / 256 / 512, nearCacheMips(size) mips, RENDER_TARGET | SHADER_RESOURCE, GENERATE_MIPS, one slice, no MSAA);
    // nearCache[1] given exactly when needSecond, of nearCache[0]'s size. Otherwise BadLayout (before anything is drawn). Not owned.
    ID3D11Texture2D* nearCache[2] = {nullptr, nullptr};
    // Issue 0371, tests and replays only (the product never sets it): the EXE's own macro-shadow result of the tile (512 x
    // 512 R8G8B8A8, its mip 0 is used) replaces the target's mip 0 after passes 3 and 3a, before its mips. For a tile whose
    // macro-shadow inputs (its decals) a capture does not hold; the passes are checked on their own elsewhere. Another
    // shape: BadLayout. Null: no effect. Not owned.
    ID3D11Texture2D* macroShadowGiven = nullptr;
    PostConstants k;
};

struct PostOutputs {
    ID3D11Texture2D* t0 = nullptr;          // BC1_TYPELESS, sizeClass^2, cacheMips(sizeClass) mips; caller releases
    ID3D11Texture2D* t8 = nullptr;          // same, only when needSecond
};

// ---- CacheBake's per-style inputs from the player's install (terrain-bake-passes.md 5.2).
// Style = the lowest set bit 1..4 of tile+0x138, 0 when none: 0 Generic, 1 America, 2 Asia, 3 Africa, 4 Europe.
const char* styleName(uint32_t style);          // nullptr for style >= 5
std::string sunlightFile(uint32_t style);       // "Sunlight_<Style>_Terrain.dds"
std::string shadowlightFile(uint32_t style);    // "Shadowlight_<Style>_Terrain.dds", "_Terrain_2.dds" for America/Asia/Europe
std::string colorKeyFile(uint32_t style);       // "Terrain_<Style>_ColorKey.tga"
constexpr size_t kColorKeyTexels = 32 * 32 * 32;
// The EXE's ColorKey volume from a 32-bit uncompressed TGA: the image in reading order (top row first), BGRA -> RGBA,
// its first 32^3 pixels as the volume with x fastest, then y, then z. Errors: Truncated, Unsupported (other TGA types,
// bit depths, right-to-left), BadLayout (fewer than 32^3 pixels). On error `rgba` is empty.
Status buildColorKeyLut(const uint8_t* tga, size_t size, std::vector<uint8_t>& rgba);

// The three views CacheBake takes per style; plain pointers owned by the caller (call release()).
struct StyleInputs {
    ID3D11ShaderResourceView* sunCube = nullptr;     // BC1_UNORM_SRGB cube view, as the EXE's sampler id 2
    ID3D11ShaderResourceView* shadowCube = nullptr;
    ID3D11ShaderResourceView* colorKey = nullptr;    // R8G8B8A8_UNORM 32^3
    void release();
};
// Reads the style's files through c5r::gamefs. Errors: BadLayout (no device, style >= 5, not a cube), NotFound (a file
// missing), the parsers' errors, Device. On error `out` holds nothing.
Status loadStyleInputs(ID3D11Device* dev, const gamefs::GameFiles& fs, uint32_t style, StyleInputs& out);
// Ticket 0698: loadStyleInputs in two halves, so that the pipeline's loader thread reads the files off the render thread
// (research/05-graphics-composition/terrain-bake-slow-first-use.md section 8). readStyleFiles reads the three files and
// keeps each read's result (NotFound naming the file, as loadStyleInputs; style >= 5 BadLayout) and returns the first
// failure; it needs no device. createStyleInputs makes the views with loadStyleInputs' checks in its order, so the failure
// it reports is the one loadStyleInputs reports. loadStyleInputs = readStyleFiles + createStyleInputs.
struct StyleFiles {
    uint32_t style = 0;
    std::vector<uint8_t> sun, shadow, colorKey;   // the Sunlight cube DDS, the Shadowlight cube DDS, the ColorKey TGA
    Status sunRead, shadowRead, colorKeyRead;
    uint64_t bytes() const { return sun.size() + shadow.size() + colorKey.size(); }
};
Status readStyleFiles(const gamefs::GameFiles& fs, uint32_t style, StyleFiles& out);
Status createStyleInputs(ID3D11Device* dev, const StyleFiles& files, StyleInputs& out);

// Intermediate targets, kept after run() for inspection (tests): valid until the next run() or release().
enum class PostTarget { Normal, Shadow, Cache0, Cache1, Blocks0, Blocks1 };

class BakePost {
public:
    BakePost();
    ~BakePost();
    BakePost(const BakePost&) = delete;
    BakePost& operator=(const BakePost&) = delete;

    Status init(ID3D11Device* device, const shaders::ShaderSection& exe, const PostShaderVas& vas = PostShaderVas());
    Status run(ID3D11DeviceContext* ctx, const PostInputs& in, PostOutputs& out);
    ID3D11Texture2D* target(PostTarget which) const;  // nullptr before the first run
    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::bake
