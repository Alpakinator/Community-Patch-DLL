// c5r/forest/draw.h - the forest and jungle trees drawn as the EXE draws them, with the EXE's own ForestShader (epic
// 0431, route A, ticket 4551).
//
// Facts it rests on: research/05-graphics-composition/trees.md (2.1 the files, 2.2 the life cycle and the 90-card draw
// objects, 2.4 the draw, 9 the design, 11 this module: the cb1 store map of the render setup 0x63A7A0, the card choice of
// 0x6C1CD0, the texture slots and the ColorKey index map). Own code written from those descriptions of behaviour (D83):
// the shader blobs are read from the player's CivilizationV_DX11.exe at run time (c5r::shaders), the cards from the
// player's `_2` .gr2 files (c5models) and the textures from the player's install (c5r::gamefs); nothing of the EXE or the
// game's files is in the repository.
//
// What the module does: init() creates the EXE's ForestShader VS (VA 0x0676C6A0) and PS (0x0676D388) after checking their
// stage, declarations, slots and the cb sizes against the blobs, the input layout of the 36-byte tree vertex, stock's
// states and samplers, the 16 `_2` textures (t0, t1 of 5 style entries x 2 kinds; entries 0 and 4 share the Europe
// files) and the 10 immutable card buffers (90 cards per style entry and kind, chosen with a caller-seeded CRT rand() in
// the EXE's order; or given from outside with replaceCards for a capture test). draw() fills one cb1 block per view
// (computeForestFrame) and draws every given block (90 trees: x, y, z, scale) with DrawIndexedInstanced(card indices, 1)
// in every view, at the place the caller chooses (stock: after the terrain, the water, the ice and the Screen Overlay
// pass, before the units and the cloud planes; trees.md 11.1).
//
// Placement (where the trees stand, which are hidden) is not here: the caller gives the cb7 blocks (ticket 4550 makes
// them; the scene, 4554, joins the two). This header does not include the placement's.
//
// Conventions: as c5r/terrain/terrain.h (D3DX row-major matrices, row vectors, the EXE's world units and axes).
// Errors: every call returns a c5r::Status; nothing throws. A missing input fails with a reason naming it (D118 / D125:
// no silent fallback, no constant stand-in texture). A failed call leaves nothing half-made: a failed init() leaves the
// renderer uninitialised, a failed replaceCards() keeps the previous buffers, a failed draw() draws nothing.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/forest/crtrand.h"
#include "c5r/status.h"

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace forest {

const uint32_t kForestVsVa = 0x0676C6A0u;   // ForestShader VS, vs_5_0, 1,100 B, SHA-256 0783e181... [C, trees.md 11.1]
const uint32_t kForestPsVa = 0x0676D388u;   // ForestShader PS, ps_5_0, 2,264 B, SHA-256 95325aff...
const uint32_t kFrameBytes = 240;           // cb1 of the tree draws: 15 rows (trees.md 11.1)
const uint32_t kSlots = 90;                 // trees per block (cb7 rows; BLENDINDICES.x of a card's vertices)
const uint32_t kBlockFloats = kSlots * 4;   // x, y, z, scale per tree
const uint32_t kBlockBytes = kBlockFloats * 4;   // 1,440 B: cb7
const uint32_t kStride = 36;                // POSITION, NORMAL (R32G32B32), TEXCOORD0 (R32G32), BLENDINDICES (R8G8B8A8_UINT)
const uint32_t kCardVertexBytes = 32;       // a card vertex as the mesh holds it: position, normal, uv (8 floats)
const uint32_t kEntries = 5;                // style entries = the terrain tile's style byte: Generic, America, Asia, Africa, Europe
const uint32_t kKinds = 2;                  // 0 forest, 1 jungle
const uint32_t kMaxViews = 4;               // g_NumViews 0xD90FA4 (the camera block carries up to 4)
const uint32_t kLuts = 5;                   // ColorKey volumes per type, by style (asset-pipeline.md 3.5)

// The card kind of a block (named CardKind since ticket 4554: placement.h's Kind is the plot record's kind 1-4).
enum CardKind : uint32_t { kForest = 0, kJungle = 1 };

// The ColorKey LUT index of a style entry ({0, 3, 2, 4, 1}, 0x6C1CD0; trees.md 11.1): t5 = Terrain[lutIndex(e)], t8 =
// TerrainFOW[lutIndex(e)]. kLuts for an entry >= kEntries.
uint32_t lutIndex(uint32_t entry);
// The install's file names of an entry and kind (0x780D80): the model `<kind>_<style>_2.gr2`, t0 `<kind>_<style>_2.dds`, t1
// `<kind>_overlay_<style>_2.dds`, style = europe, america, asia, africa, europe for entries 0..4. "" outside.
const char* modelFile(uint32_t entry, uint32_t kind);
const char* diffuseFile(uint32_t entry, uint32_t kind);
const char* overlayFile(uint32_t entry, uint32_t kind);

// CrtRand (the MSVC CRT's rand(), the caller seeds it): c5r/forest/crtrand.h, shared with the placement (ticket 4554).

// One card: a mesh of a `_2` model, its vertices as 8 floats each (position, normal, uv) and its triangle indices.
struct Card {
    std::string name;
    std::vector<float> vertices;     // 8 x vertexCount
    std::vector<uint32_t> indices;   // 3 x triangles, each < vertexCount
    uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size() / 8); }
};

// Every mesh of a `_2` model file in the file's mesh order (c5models). Errors: the reader's (BadMagic, Truncated,
// BadLayout as c5models reports them, `name` in the reason), BadLayout (no mesh; a mesh without Real32 Position[3],
// Normal[3] and TextureCoordinates0[2..4] (the `_2` files store 3: u, v and an unused w; the card takes u, v); no vertices; an index count that is 0 or not a multiple of 3; an index past the
// mesh's vertices; a value not finite; a card above 65,535 / 90 vertices, which 90 cards could not address in 16 bits).
// On failure `out` is empty.
Status cardsFromGr2(const uint8_t* data, size_t size, const std::string& name, std::vector<Card>& out);

// The EXE's card choice (0x6C1CD0): for each entry 0..4 and each slot 0..89, first forest[e][k] = rand() % nForest[e]
// (the entry's forest model's mesh count), then jungle[e][k] = rand() % nJungle[e] (900 calls). A count of 0 gives 0 and
// still draws the call (so the sequence stays stock's).
void chooseCards(CrtRand& r, const uint32_t nForest[kEntries], const uint32_t nJungle[kEntries],
                 uint32_t forest[kEntries][kSlots], uint32_t jungle[kEntries][kSlots]);

// The bytes of one immutable VB / IB pair (0x6C1CD0): the chosen cards one after the other, each vertex = the card's 32
// bytes, BLENDINDICES (slot, 0, 0, 0); the VB sized (largest card's vertex count x 90) vertices, the IB (largest card's
// index count x 90) 16-bit indices (each card's indices plus its first vertex), tails zero (stock leaves stale bytes
// there; the draw never reads them). indexCount = the indices of the 90 cards (2,160 in every stock capture).
struct CardBuffers {
    std::vector<uint8_t> vb;
    std::vector<uint8_t> ib;
    uint32_t indexCount = 0;
};
// Errors: BadLayout (no cards; a choice >= the card count; a card with no vertices or indices; the VB above 65,536
// vertices). On failure `out` is unchanged.
Status buildCardBuffers(const std::vector<Card>& cards, const uint32_t choice[kSlots], CardBuffers& out);

// The DistanceFog globals rows 13-14 take (0xDA1C50 .. 0xDA1C6C; as c5r::ice::Fog). Defaults: the stock values [L].
struct Fog {
    float color[4] = {146.0f * (1.0f / 255.0f), 182.0f * (1.0f / 255.0f), 243.0f * (1.0f / 255.0f), 1.0f};
    float density = 0.05f;   // 0xDA1C60
    float scale = 5.0e-5f;   // 0xDA1C64
    float w = -1.0f;         // 0xDA1C6C, the zoom fraction; < 0: water::zoomFraction of view 0's camera z
};

// One view of the frame (v < g_NumViews): g_View[v] 0xDA1310 + 0x40v, its camera position 0xDA1440 + 0x40v (false: row 3
// of the view's inverse, about 2 ulp from the EXE's), OverlayProj[v] 0xDA1690 + 0x40v (the overlay pair's, as the
// terrain binds it).
struct View {
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool haveCamPos = false;
    float camPos[3] = {0, 0, 0};
    float overlayProj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct FrameInputs {
    float proj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // g_Proj 0xDA12D0
    uint32_t width = 0, height = 0;   // the target's size: the viewport and cb1 row 9 (the t3 uv)
    uint32_t numViews = 1;            // 1 .. kMaxViews
    View views[kMaxViews];
    float fowAnim[4] = {0, 0, 0, 0};                               // 0xDA1C40..4C (c5r::fow::fowAnim)
    Fog fog;
    float overlayAlpha = 0.6f;                                     // forests.xml overlay_alpha (Forest+0x134)
    float auxDir[3] = {0.4330127f, -0.25f, 0.8660254f};            // 0xD8BD0C..14, row 11 (stored, read by no shader)
    // The frame's shared inputs, required (draw fails naming a missing one): t3 the Screen Overlay [0xDA12A8] (the
    // target's size, RGBA8, trees.md 11.1, made by the caller's screen-overlay pass), t4 the distance-fog ramp [0xDA12B4],
    // t6 the FOW mask [0xDA12AC], t7 the cloud base [0xDA12B0]; per LUT index (lutIndex of the entries drawn) t5 the
    // Terrain ColorKey LUT [0xD90E4C + 4i] and t8 the TerrainFOW LUT [0xD90E60 + 4i] (32^3). t2 the Terrain Overlay
    // [0xDA12A4] is bound like stock's but read by no shader: optional.
    ID3D11ShaderResourceView* screenOverlay = nullptr;
    ID3D11ShaderResourceView* fogRamp = nullptr;
    ID3D11ShaderResourceView* fowMask = nullptr;
    ID3D11ShaderResourceView* cloudBase = nullptr;
    ID3D11ShaderResourceView* colorKeyLut[kLuts] = {};
    ID3D11ShaderResourceView* fowLut[kLuts] = {};
    ID3D11ShaderResourceView* terrainOverlay = nullptr;
};

// cb1 of view v byte for byte as 0x63A7A0 fills it (trees.md 11.1); the 28 bytes it does not store (rows 9.zw, 10.yzw,
// 11.w, 12.w) are 0. Bit-equal to the EXE in every stored byte when the view-projection is given exactly (proj
// identity), the camera position given, except row 14.y (x87 exp, at most 1 ulp); the module's own V * P follows
// D3DXMatrixMultiply's scalar order. Errors: BadLayout (out null, v >= numViews or numViews outside 1..kMaxViews, width
// or height 0, a matrix, the camera, FOWAnim, the fog, overlay_alpha or the direction not finite, the fog density 0, the
// view not invertible).
Status computeForestFrame(const FrameInputs& in, uint32_t view, uint8_t out[kFrameBytes]);
// True when 0x63A7A0 stores byte `off` of cb1 (the mask of a comparison with a captured block).
bool forestFrameStored(uint32_t off);

// The input layout the module creates (stock's draws show it [L]).
const D3D11_INPUT_ELEMENT_DESC* forestInputLayout(uint32_t* count);

// One block of a plot to draw: its style entry (the tile's style byte, 0..4), kind, and cb7 (90 x (x, y, z, scale);
// scale 0 hides a tree, as stock).
struct Block {
    uint32_t entry = 0;
    uint32_t kind = kForest;
    float trees[kBlockFloats] = {};
};

struct InitOptions {
    uint32_t vsVa = kForestVsVa;   // for the negative tests and another EXE build
    uint32_t psVa = kForestPsVa;
    uint32_t randSeed = 1;         // the CrtRand state the card choice starts from (srand(randSeed))
};

struct FrameStats {
    uint32_t views = 0, draws = 0;
    uint64_t indices = 0;
    double cpuMs = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Errors: BadLayout (dev null; a blob of another stage; a declaration, slot or cb size unlike stock's, named),
    // NotFound (a blob not in the section; a model or texture file not in the install, named), the parsers' errors
    // (DDS / .gr2, the file named; a texture that is not a plain 2D texture: Unsupported), cardsFromGr2's, Device /
    // OutOfMemory.
    Status init(ID3D11Device* dev, const shaders::ShaderSection& sec, const gamefs::GameFiles& fs,
                const InitOptions& o = InitOptions());
    bool ready() const;

    // Replaces the card buffers of (entry, kind) with given bytes (a capture's VB and IB; stride 36, R16 indices).
    // Errors: NotFound (init not done), BadLayout (entry or kind out of range; VB empty or not whole vertices, IB not
    // whole indices; indexCount 0, not a multiple of 3 or past the IB; an index of the first indexCount past the VB; a
    // BLENDINDICES.x of a used vertex >= 90), Device. On failure the previous buffers stay.
    Status replaceCards(uint32_t entry, uint32_t kind, const std::vector<uint8_t>& vb, const std::vector<uint8_t>& ib,
                        uint32_t indexCount);

    // Draws every block in every view into rtv / dsv (stock's states: depth LESS_EQUAL with write, SRC_ALPHA /
    // INV_SRC_ALPHA; dsv may be null). The context's state is changed and not restored. Errors: NotFound (init not done;
    // a shared input view null, named; the LUT of a drawn entry's lutIndex null, named), BadLayout (ctx or rtv null, a
    // block's entry or kind out of range, a block value not finite, as computeForestFrame), Device. Nothing is drawn
    // when any check fails (every check and every cb1 comes before the first draw).
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                const FrameInputs& in, const Block* blocks, size_t count, FrameStats* stats);

    // For the tests and a later harness: the module's t0 (slot 0) / t1 (slot 1) view of (entry, kind); the card set of a
    // kind and style entry's model; the chosen cards, the buffer bytes and the index count of (entry, kind); the cb1
    // blocks of the newest draw (one per view). Empty / nullptr before init or out of range.
    ID3D11ShaderResourceView* texture(uint32_t entry, uint32_t kind, uint32_t slot) const;
    const std::vector<Card>& cards(uint32_t entry, uint32_t kind) const;
    const std::vector<uint32_t>& choice(uint32_t entry, uint32_t kind) const;
    const CardBuffers& buffers(uint32_t entry, uint32_t kind) const;
    const std::vector<std::vector<uint8_t>>& frames() const;

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::forest
