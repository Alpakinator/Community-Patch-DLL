// c5r/ice/ice.h - the polar ice drawn as the EXE draws it, with the EXE's own IceShader (epic 0169, seam G, issue 4306).
//
// Facts it rests on: research/05-graphics-composition/polar-ice.md (section 2 what stock draws, 3 the mesh and the Ice
// object 0xD68F9C, 5 the route, 7 IceFrame row by row from the draw 0x5FACB0, 8 this module), asset-pipeline.md 3.2
// (IceShader = material 59), fog-of-war.md 3.6 (FOWAnim). Own code written from those descriptions of behaviour (D83):
// the shader blobs are read from the player's CivilizationV_DX11.exe at run time (c5r::shaders) and the five texture
// files from the player's install (c5r::gamefs); nothing of the EXE or the game's files is in the repository.
//
// What the module does: init() creates the EXE's IceShader VS (VA 0x067A07E8) and PS (0x067A1688) after checking their
// stage, declarations, slots and the IceFrame size against the blobs, the input layout of the ice's 8-byte vertex, the
// states of stock's ice draw and the module's own seven textures (t0-t6). setMesh() takes the ice mesh the EXE built at
// load (the tap of issue 4307: the IMMUTABLE VB and R16 IB, the chunk records and the Ice object's fields). draw() fills
// one IceFrame per map copy (1, or 3 when the map wraps) from the camera, the object's fields and the frame's globals
// (computeIceFrame, the row map of polar-ice.md 7) and draws every chunk of every copy with one DrawIndexed, at the place
// the caller chooses (stock: after the ocean, before the second terrain pass, the waves and the models).
//
// Conventions: as c5r/terrain/terrain.h (D3DX row-major matrices, row vectors, the EXE's world units and axes).
// Errors: every call returns a c5r::Status; nothing throws. A missing input fails with a reason naming it (D118 / D125:
// no silent fallback, no constant stand-in texture). A failed call leaves nothing half-made: a failed init() leaves the
// renderer uninitialised, a failed setMesh() keeps the previous mesh (or none), a failed draw() draws nothing.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/terrain/terrain.h"

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace ice {

using Camera = terrain::Camera;

const uint32_t kIceFrameBytes = 480;          // IceFrame, shared id 42, 30 rows (polar-ice.md 2, 7) [C, L]
const uint32_t kIceVsVa = 0x067A07E8u;        // IceShader VS, vs_5_0, 1,248 B [C 2903, L 4301]
const uint32_t kIcePsVa = 0x067A1688u;        // IceShader PS, ps_5_0, 8,824 B
const uint32_t kIceStride = 8;                // TEXCOORD0 R16G16_SNORM @0, TEXCOORD1 R8G8B8A8_UNORM @4 [L]
const uint32_t kMaxCopies = 3;                // the draw's map copies k = 0 (centre), 1 (+W), 2 (-W) [C 0x5FACB0]

// The Ice object's fields the draw reads, as the tap copies them: Ice+0xFE80 .. Ice+0x1001F (0x1A0 bytes; polar-ice.md
// 7 and 8). The draw reads +0xFF0C .. +0x1001F; the head (+0xFE80 .. +0xFF0B: built flag, handles) is carried for the
// tap's own checks and the tests.
const uint32_t kFieldsBase = 0xFE80;
const uint32_t kFieldsEnd = 0x10020;          // exclusive
const uint32_t kFieldsSize = kFieldsEnd - kFieldsBase;

struct IceFields {
    uint8_t raw[kFieldsSize] = {0};
    bool valid = false;
    // The float at Ice+off (kFieldsBase <= off <= kFieldsEnd - 4, 4-aligned); 0 outside.
    float get(uint32_t off) const;
    void set(uint32_t off, float v);
};

// The Ice object's offsets the draw reads (polar-ice.md 7).
namespace field {
const uint32_t kLeanScale = 0xFF0C;       // row 16.x (equals the .lean files' +0x04 float, 0.18431 [L])
const uint32_t kWaterHeight = 0xFF14;     // rows 7.x, 21.x (0xD91274)
const uint32_t kBox = 0xFF1C;             // rows 4.xy, 5.xy: the whole mesh's box min x, min y, max x, max y
const uint32_t kDirA = 0xFF38;            // row 14.xyz
const uint32_t kDirB = 0xFF44;            // row 15.xyz
const uint32_t kHeightAbove = 0xFFC0;     // row 6.x = this + kDepthBelow (25 [L])
const uint32_t kDepthBelow = 0xFFC4;      // row 7.x = water - this (LargePolyDepth, 8 [L])
const uint32_t kTextureRepeat = 0xFFD8;   // row 17.x
const uint32_t kBumpRepeat = 0xFFDC;      // row 18.x
const uint32_t kSpecularPower = 0xFFE0;   // row 19.x
const uint32_t kSideTexRepeat = 0xFFE4;   // row 22.xy (U, V)
const uint32_t kSideBumpRepeat = 0xFFEC;  // row 23.xy (U, V)
const uint32_t kSubsurface = 0xFFF4;      // row 20.xyz ((c / 255)^2.2)
const uint32_t kUnderwater1 = 0x10000;    // row 24.xyz
const uint32_t kUnderwater2 = 0x1000C;    // row 25.xyz
const uint32_t kColorRamp = 0x10018;      // row 26.x (1 / Underwater_ColorRamp)
const uint32_t kAlphaRamp = 0x1001C;      // row 27.x (1 / Underwater_AlphaRamp)
}

// One chunk of the mesh: the chunk array's box (Ice+0xFF2C entry +0x00..+0x0C, used only for the cull) and its draw
// record's +0x0C triangles, +0x10 start index, +0x14 base vertex (polar-ice.md 3).
struct Chunk {
    float box[4] = {0, 0, 0, 0};   // min x, min y, max x, max y
    uint32_t triangles = 0;
    uint32_t startIndex = 0;
    int32_t baseVertex = 0;
};

// The mesh as the tap's reader (issue 4307) fills it (polar-ice.md 8): the VB's and IB's bytes as the EXE created them
// (stride 8, R16 indices), the chunks in the EXE's order, the object's fields.
struct Mesh {
    std::vector<uint8_t> vb;
    std::vector<uint8_t> ib;
    std::vector<Chunk> chunks;
    IceFields fields;
};

// The DistanceFog globals rows 28-29 take (0xDA1C50 .. 0xDA1C6C; as c5r::water::Fog). Defaults: the stock values [L],
// the colour as byte * (1/255) in float: bit-equal to the captured row 28 (243 / 255 as a float division is one ulp low,
// the error of water.h's Fog that C1's A4 names).
struct Fog {
    float color[4] = {146.0f * (1.0f / 255.0f), 182.0f * (1.0f / 255.0f), 243.0f * (1.0f / 255.0f), 1.0f};  // 0xDA1C50..5C
    float density = 0.05f;   // 0xDA1C60
    float scale = 5.0e-5f;   // 0xDA1C64
    float w = -1.0f;         // 0xDA1C6C = 0xD90FAC, the zoom fraction; < 0: water::zoomFraction of the fog camera z
};

struct FrameInputs {
    // view and proj: the scene's 0xDA1310 / 0xDA12D0, or for the reflection pass 0xDA1990 / 0xDA1A90; width, height:
    // the target's size (the viewport).
    Camera cam;
    bool haveCamPos = false;       // false: row 3 of the view's inverse (as the EXE computes it; 2 ulp)
    float camPos[3] = {0, 0, 0};
    bool haveFogCamZ = false;      // [0xDA1448 + 0x40k]: the SCENE camera's z, also in the reflection call; false:
    float fogCamZ = 0.0f;          // camPos z
    float overlayProj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // 0xDA1690 (view 0's, both calls)
    float fowAnim[4] = {0, 0, 0, 0};                                             // 0xDA1C40..4C (c5r::fow::fowAnim)
    Fog fog;
    float worldWidth = 0.0f;       // [0xD90FD8] - [0xD90FCC]; > 0 when wrapX
    bool wrapX = false;            // [0xD90FBC] != 0: three copies
    bool haveCullBox = false;      // the cull box of copy 0 (0xDA1B60, 0xDA1B64, 0xDA1B90, 0xDA1B94); false: every
    float cullBox[4] = {0, 0, 0, 0};   // chunk of every copy is drawn (the same pixels, more work)
    // The frame's shared inputs, required (draw fails naming a missing one): t7 the distance-fog ramp [0xDA12B4], t8 the
    // FOW mask [0xDA12AC], t9 the cloud base [0xDA12B0], t10 the TerrainFOW ColorKey LUT [0xD90E60] (32^3).
    ID3D11ShaderResourceView* fogRamp = nullptr;
    ID3D11ShaderResourceView* fowMask = nullptr;
    ID3D11ShaderResourceView* cloudBase = nullptr;
    ID3D11ShaderResourceView* fowLut = nullptr;
};

// IceFrame of map copy k (0, 1, 2) byte for byte as 0x5FACB0 fills it (polar-ice.md 7); the 164 bytes the draw does not
// store are 0. Bit-equal to the EXE in every stored byte when the view-projection is given exactly (proj identity),
// camPos given and the fog camera z given, except row 29.y (x87 exp, 1 ulp); the module's own (T_k * V) * P follows
// D3DXMatrixMultiply's scalar order (2 ulp relative, as WaterFrame). Errors: BadLayout (out null, copy >= 3, a matrix,
// the camera, the fog or a read field not finite, worldWidth not > 0 for copies 1 and 2, the view not invertible),
// NotFound (fields not valid).
Status computeIceFrame(const IceFields& f, const FrameInputs& in, uint32_t copy, uint8_t out[kIceFrameBytes]);
// True when 0x5FACB0 stores byte `off` of IceFrame (the mask of a comparison with a captured block).
bool iceFrameStored(uint32_t off);

// The copy's cull box and the overlap test of the draw (edges inclusive).
void copyCullBox(const FrameInputs& in, uint32_t copy, float out[4]);
bool chunkVisible(const Chunk& c, const float box[4]);

// What init() checks the EXE's programs against (polar-ice.md 8): VS input TEXCOORD0.xy + TEXCOORD1.xyzw (float), cb1
// only, at most 30 rows; PS cb1 only, exactly 30 rows (= kIceFrameBytes); PS resources t0 cube, t1 t2 t4-t9 2D, t10 3D
// (a declared slot outside this, or another dimension, fails; t3 is bound like stock's but not declared); PS samplers
// within s0-s10.
struct InitOptions {
    uint32_t vsVa = kIceVsVa;   // for the negative tests and another EXE build
    uint32_t psVa = kIcePsVa;
};

// The input layout the module creates (and stock's draws show [L]).
const D3D11_INPUT_ELEMENT_DESC* iceInputLayout(uint32_t* count);

// The texture files of slots t0-t6, as the EXE names them (t2/t3 the four- and one-channel blocks of Ice_Bump.lean, t5/t6
// those of Ice_Bump_Sides.lean). Views as stock's [L]: t0 BC1 cube sRGB, t1 / t4 BC3 sRGB, t2 / t5 R8G8B8A8_UNORM, t3 /
// t6 A8_UNORM, full mip chains.
const char* textureFile(uint32_t slot);

struct FrameStats {
    uint32_t copies = 0, draws = 0, chunksCulled = 0;
    uint64_t indices = 0;
    double cpuMs = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Errors: BadLayout (dev null; a blob of another stage; a declaration, slot or the IceFrame size not as above),
    // NotFound (a blob not in the section, a texture file not in the install, named), the parsers' errors (DDS / .lean:
    // Truncated, BadMagic, BadLayout, Unsupported; a texture of another shape than stock's: Unsupported), Device.
    Status init(ID3D11Device* dev, const shaders::ShaderSection& sec, const gamefs::GameFiles& fs,
                const InitOptions& o = InitOptions());
    bool ready() const;

    // Replaces the mesh (uploaded as IMMUTABLE buffers, as stock). Errors: NotFound (init not done), BadLayout (fields
    // not valid or a read field not finite; VB empty or not whole vertices; IB empty or not whole R16 indices; zero
    // chunks; a chunk with 0 triangles, a box not finite or min > max, its indices past the IB, a negative base vertex,
    // or an index + base vertex past the VB), Device. On failure the previous mesh stays.
    Status setMesh(const Mesh& m);
    bool hasMesh() const;
    void clearMesh();
    uint32_t chunkCount() const;

    // Draws the ice into rtv / dsv (depth LESS_EQUAL with write, as stock; dsv may be null). The context's state is
    // changed and not restored. Errors: NotFound (init, no mesh, a shared input view null - named), BadLayout (ctx or rtv
    // null, camera size 0, as computeIceFrame), Device.
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                const FrameInputs& in, FrameStats* stats);

    // The module's own view at t0-t6 (nullptr before init or for other slots), and the IceFrame blocks of the newest
    // draw (one per copy drawn), for the tests and a later harness.
    ID3D11ShaderResourceView* ownTexture(uint32_t slot) const;
    const std::vector<std::vector<uint8_t>>& frames() const;

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::ice
