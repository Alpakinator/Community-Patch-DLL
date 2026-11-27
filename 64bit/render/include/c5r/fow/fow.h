// c5r/fow/fow.h - the fog of war of the server's world renderer (epic 0168, seam F).
//
// What stock draws (research/05-graphics-composition/fog-of-war.md sections 2 and 3; behaviour only, D83):
//   1. the FOW mask: a square RGBA8 target (1024 at FOWLevel 2-3, 512 at 0-1), cleared to 0 and redrawn every frame by
//      the EXE's FOWDrawMask program as MAX-blended hexagons, one per plot of each view's plot range, in the overlay's
//      window (OverlayProj): R = explored, G = unexplored, B = visible, A = 1 where a hexagon reaches;
//   2. the terrain, water, river and model shaders read it (MaskRenderer::srv() -> terrain t4, water/river t10);
//   3. after the world and the models: FOW CULL (depth only) and three translucent cloud planes (FOWBaseLayer, the
//      install's fogcloudtile_dif_02/03/01.dds) gated by the mask's G (CloudRenderer);
//   4. at FOWLevel 3 a particle simulation (FluidParticles) drawn after the cloud planes: ParticleSim / ParticleRenderer
//      below (ticket 0340, fog-of-war.md 10), a look-alike: stock's simulation is rand()-seeded and carries history.
//      (Before 0340: "not drawn here (an intended difference, MC ruling #1131 (7))".)
// The programs are read from the player's CivilizationV_DX11.exe and the textures from the player's install at run
// time (D83, D85); nothing of either is in this repository.
//
// Owners: this header's shape is the lead's (lead-0168); MaskRenderer is built by task 3952 (render/src/fow/mask*.cpp),
// CloudRenderer by task 3955 (render/src/fow/cloud*.cpp). Until they land, render/src/fow/mask.cpp and cloud.cpp are
// stubs whose init() returns Err::Unsupported, so the server's scene (task 3954) can build and test its failure route.
#pragma once
#include <c5r/status.h>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace c5r {
namespace fow {

// The EXE's per-plot state (fog-of-war.md 3.2): 0 visible, 1 seen (revealed, not visible), 2 unexplored.
enum class PlotState : uint8_t { Visible = 0, Seen = 1, Unexplored = 2 };

// The server's PlotRecord::fow (bit 0 revealed, bit 1 visible) to the EXE's state, as the EXE's HexFOWStateChanged
// handler maps the event's mode byte (3 -> visible, 2 -> visible, 1 -> seen, 0 -> unexplored). Bits above 1 are
// ignored here; the caller counts them as a fault (design 5.5).
inline PlotState stateFromFowBits(uint8_t bits)
{
    switch (bits & 3u) {
    case 3: case 2: return PlotState::Visible;
    case 1: return PlotState::Seen;
    default: return PlotState::Unexplored;
    }
}

// FOWAnim (TerrainFrame 0x090; model cb row 12): (k, k, -2 T, 0) with k = floor(W * 0.002) / W, W the map's world
// width, T the accumulated frame time in seconds while the map is drawn (fog-of-war.md 2.5, 3.6). Stock's T restarts
// per process, so only the phase differs. worldWidth <= 0 gives stock's value before a game, 0.002 everywhere.
inline void fowAnim(float worldWidth, double seconds, float out[4])
{
    if (!(worldWidth > 0.0f)) { out[0] = out[1] = out[2] = out[3] = 0.002f; return; }
    const float k = std::floor(worldWidth * 0.002f) / worldWidth;
    out[0] = k; out[1] = k; out[2] = static_cast<float>(-2.0 * seconds); out[3] = 0.0f;
}

// One view of the frame (view 0 and, at the map's wrap seam, the wrap view): the overlay's window and the camera.
struct View {
    float overlayProj[16] = {};   // world -> overlay NDC, row-major as the EXE stores it (row vectors), from 0167's
                                  // overlay pair (OverlayFrame::overlayProj[v])
    float viewProj[16] = {};      // world -> clip of this view, row-major (row vectors); CloudRenderer, and (3952) the
                                  // mask's plot range by stock's rule (plotRangeFromCamera)
    // The view's plot range [x0, x1) x [y0, y1) in grid coordinates; x may run past the map width (wrapped). When
    // x1 <= x0 the renderer derives it (3952, fog-of-war.md 7.1): from viewProj by stock's rule (plotRangeFromCamera)
    // when viewProj is set (not all zero) and the map's worldWidth and worldHeight are > 0, else from overlayProj
    // (plotRangeFromWindow, wider than stock's).
    int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

static constexpr uint32_t kMaxViews = 4;

// The map: plot states row-major (index y * width + x), and the hex-to-world placement the server's grid already
// uses (the EXE's HexToWorld; fog-of-war.md 3.3 and 7.1).
struct MapInput {
    const PlotState* states = nullptr;
    uint32_t width = 0, height = 0;   // plots
    bool wrapX = true;
    float worldWidth = 0.0f;          // for fowAnim(); (3952) also the wrap period [0xD90FE4] of stock's range rule
    // (3952) The world box height [0xD90FE8] (c5r::water::World::height = (samplesY - 1) sy: 2216.094 on Tiny); with
    // worldWidth (World::width = (samplesX - 1) sx, 3583 on Tiny) it places the footprint for plotRangeFromCamera.
    float worldHeight = 0.0f;
    // (3952) The number of entries behind `states`; must equal width * height (a table of another size is refused).
    size_t stateCount = 0;
    // (3952) The placement of a plot's hexagon in world: centre = (hx, hy, -hx-hy, 1) x hexToWorld, then x * sx - ox,
    // y * sy - oy, with hx = gridX - (gridY >> 1), hy = gridY (camera-and-picking.md 5). hexToWorld is the EXE's M_h2w
    // (0xD91030, row-major as stored, row vectors), gridScale = GridHeightmap (sx, sy) +0xA8/+0xAC, gridOrigin = (ox, oy)
    // +0xB4/+0xB8: c5r::heightfield::HeightGrid carries all five (hexToWorld, sx, sy, ox, oy). nominalPlacement()
    // fills the textbook values for maps without a grid (tests, tools).
    float hexToWorld[16] = {};
    float gridScale[2] = {1.0f, 0.866f};
    float gridOrigin[2] = {0.0f, 0.0f};
};

// (3952) The textbook placement of a width x height map (the EXE's values to within 0.003 world units: M_h2w rows
// (64, 0), (32, 64), 0, (32, 128); sx 1, sy 0.866; ox = 32 width, oy = 32 (height + 4) 0.866, the grid's 4 margin rows).
// The live M_h2w differs from these by up to 0.0033 (Tiny ground truth 2026-11-11): the server passes the grid's own.
void nominalPlacement(MapInput& map);

// (3952) PlotRecord::fow bytes -> states (stateFromFowBits). Returns the number of bytes with bits above 1 set (each
// still converted by its low two bits); the caller counts them as a fault (design 5.5). out must hold n entries.
uint32_t convertFowBits(const uint8_t* bits, size_t n, PlotState* out);

// (3952) Stock's plot range [r0, r1) x [r2, r3) of a view (fog-of-war.md 7.1; [C] the view footprint 0x5D0370, [L] equal
// to the drawn plots of 4 captures at 2 cameras): the bounding box of the four frustum corner rays of view.viewProj cut
// with z = 0 and z = 127.5; its min and max corners to grid plots (n = ((x + worldWidth / 2) / sx, (y + worldHeight / 2)
// / sy) through the inverse of the hex placement, cube rounding); X [gxMin - 2, gxMax + 2) clamped to [0, width],
// Y [gyMin - 1, gyMax + 1) unclamped. Errors: BadLayout (viewProj not finite or singular, a corner ray parallel to the
// ground, worldWidth or worldHeight not > 0, the placement missing or not finite, an empty map).
Status plotRangeFromCamera(const MapInput& map, const View& view, int32_t range[4]);

// (3952) The fallback when no camera is given: every plot whose outer (1.40x) hexagon's bounding box meets the view's
// OverlayProj window, as a grid rectangle (x may run past the map width). Wider than stock's range: its rim plots draw
// ramps where stock leaves the clear (0, 0, 0, 0). Errors: BadLayout (overlayProj not finite or a zero scale, the
// placement missing or not finite, an empty map).
Status plotRangeFromWindow(const MapInput& map, const View& view, int32_t range[4]);

// ---- Ticket 0674: the mask's window made from the camera (research/05-graphics-composition/pan-coherence.md 3) ----
// The server draws EXE frame N with the overlay pair of frame N-1 (decision 2 (i)); when camera N sees ground outside the
// pair's window (a zoom-out step, a fast pan, a far jump) the mask needs a window that holds camera N's view.

// The ground footprint of a view in world units (the shaders' world space, origin at the map centre): the bounding box of
// the four frustum corner rays of view.viewProj cut with z = 0 and z = 127.5, the box plotRangeFromCamera takes its plot
// range from [C 0x5D0370]. Errors: BadLayout (viewProj not finite or singular, a corner at infinity, a corner ray parallel
// to the ground, a footprint not finite).
struct Footprint {
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;   // min x, max x, min y, max y
};
Status footprintFromCamera(const View& view, Footprint& out);

// The OverlayProj of the world window [x0, x1] x [y0, y1] in the EXE's form (overlay-system.md 4): diag(2 / (x1 - x0),
// 2 / (y1 - y0), -0.5, 1) with the last row (-(x0 + x1) / (x1 - x0), -(y0 + y1) / (y1 - y0), 0.5, 1), row-major.
void windowProjFromBox(double x0, double x1, double y0, double y1, float overlayProj[16]);

// The EXE's window for a camera: per axis [floor64(min) - 64, ceil64(max)] of footprintFromCamera (multiples of 64 world
// units in origin and size, fog-of-war.md 9.5), as an OverlayProj (windowProjFromBox). It reproduces the windows of
// stock's two captured Tiny cameras (render/tests/fow/mask_test.cpp window_from_camera_stock_cameras [C]). footprint
// (optional) receives the footprint. Errors: footprintFromCamera's.
Status windowFromCamera(const View& view, float overlayProj[16], Footprint* footprint = nullptr);

// True when the footprint lies inside overlayProj's window, at most `tolerance` world units outside on any side. False
// for a window that is not finite or has a zero x or y scale.
bool footprintInWindow(const Footprint& fp, const float overlayProj[16], double tolerance = 0.01);

// For a texel at uv of a target placed by window `to`: the uv of the same world point in a target placed by window
// `from` (u = 0.5 NDC.x + 0.5, v = 0.5 - 0.5 NDC.y, fog-of-war.md 2.1), uv_from = a uv_to + b per axis. Errors: BadLayout
// (a matrix not finite, a zero x or y scale, an x-y cross term: not an axis-aligned window).
struct UvMap {
    float a[2] = {1.0f, 1.0f};
    float b[2] = {0.0f, 0.0f};
};
Status uvMapBetweenWindows(const float from[16], const float to[16], UvMap& out);

// Ticket 0793 (D269 step G, research/05-graphics-composition/overlay-history.md): the overlay's ground retained. The
// map's ground box as the overlay history keys it: world units (the shaders' world space, the camera block's world box);
// wrapPeriod > 0 for a map that wraps in x (the history then holds one period: x and x + k wrapPeriod are one ground).
struct OverlayWorld {
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    double wrapPeriod = 0;
    uint32_t key = 0;   // the map's load (the camera block's gridSeq): a new key clears the history
};

// Counters of the overlay history (ticket 0793), cumulative since the resampler was made.
struct OverlayHistoryStats {
    uint64_t absorbed = 0;        // overlays written into the history (remember() calls that drew)
    uint64_t skipped = 0;         // remember() calls that did not draw (the same key, or the same window inside the refresh)
    uint64_t rememberFails = 0;   // remember() calls refused (a bad input or a device error)
    uint64_t clears = 0;          // the history made empty (a new map key or box, release(), forget())
    uint64_t strips = 0;          // resample() calls whose target window reaches past the source's (a leading edge)
    uint64_t stripFilled = 0;     // ... whose whole edge the history held (no transparent ground in the edge)
    uint64_t stripHoles = 0;      // ... with ground in the edge the history never held (transparent there, as before 0793)
    uint64_t stripCells = 0;      // edge cells (32 x 32 world units) over all strips
    uint64_t stripCellsHeld = 0;  // ... of them the history held
    uint32_t width = 0, height = 0;   // the history's texture (0 before the first remember())
    double texelsPerUnit = 0;
};

// The overlay (the EXE's Terrain Overlay target of frame N-1, overlay-system.md) moved into another window: one
// full-screen pass into a texture of the source's size and mip count (R8G8B8A8_UNORM, RENDER_TARGET + GENERATE_MIPS; made
// at the first call, again when the source's size or mip count changes), then GenerateMips. Own programs (an HLSL string
// compiled at the first call by d3dcompiler, as the terrain's own programs), nothing of the EXE.
//
// Ticket 0793: with remember() fed every received overlay, the resampler also keeps the overlay's history: a texture of
// the whole map's ground (the store keyed by the world of the retained-world proposal, R1-R2: one wrap period by the map's
// height plus 128 world units of margin on each open side, about 1 texel per world unit, at most 16 M texels; R8G8B8A8
// colour plus an R8 "held" mask, 80 MB at most) into which each overlay is drawn at its window. resample() then fills the
// ground the source does not reach from the history where the history holds it (ground seen before in this map), and
// leaves (0, 0, 0, 0) only on ground never seen. A texel of the history changes only when a newer overlay covers it (R3:
// absence in one frame's input removes nothing); a new map key clears it. Without remember() calls resample() is as
// before 0793.
class OverlayResampler {
public:
    OverlayResampler() = default;
    ~OverlayResampler();
    OverlayResampler(const OverlayResampler&) = delete;
    OverlayResampler& operator=(const OverlayResampler&) = delete;
    // src: a Texture2D view (R8G8B8A8_UNORM) of a target placed by window `from`; the result is that target as window
    // `to` places it: each texel takes src at a uv + b (uvMapBetweenWindows; trilinear over src's mips, the level from
    // the map's scale); where a uv + b lies outside [0, 1] the history's texel of the same world point if the history
    // holds it (ticket 0793), else (0, 0, 0, 0). Errors: BadLayout (ctx or src null, src not a
    // Texture2D R8G8B8A8_UNORM view, uvMapBetweenWindows'), Device / OutOfMemory (a program, the target). On an error the
    // previous result is not valid (srv() returns null). Pipeline state: leaves IA, VS, PS (t0, t1, t2, s0, s1, b0
    // unbound), RS, OM (no target bound) and the viewport changed and HS/DS/GS unbound. Never throws.
    Status resample(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, const float from[16], const float to[16]);
    ID3D11ShaderResourceView* srv() const;   // the newest good result (null before it or after a failed call)
    void release();                          // the D3D objects (device lost, shutdown); the history with them

    // Ticket 0793: writes the overlay src, placed by window `proj` (view 0's OverlayProj as the EXE wrote it), into the
    // history of the map `world`. `frameKey` names the overlay (the pair's generation): a call with the key of the last
    // call does nothing. The same window as the last absorbed one is drawn again only every kRefreshCalls new keys (the
    // ground under it is then the source's own in resample(); the refresh keeps a changed border current for a later
    // return). The first window after a load (a new world) or forget() is drawn only after kRefreshCalls new keys at rest,
    // or at once by a call with another window: a short replay at rest does no device work at all (no programs, no
    // textures). A world with another key, box or period starts a new history (the old one is cleared when the new one
    // is first drawn; resample() reads none until then). Errors: BadLayout (ctx or src null, src
    // not a Texture2D R8G8B8A8_UNORM view, proj not an axis-aligned window, a world box not finite or empty, a wrap period
    // not finite), Device / OutOfMemory (a program, the history's textures). An error leaves the history as it was.
    // Pipeline state: as resample(). Never throws.
    static constexpr uint32_t kRefreshCalls = 30;
    Status remember(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, const float proj[16], const OverlayWorld& world,
                    uint64_t frameKey);
    void forget();                           // the history made empty (its textures kept)
    OverlayHistoryStats historyStats() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

struct MaskInput {
    MapInput map;
    const View* views = nullptr;
    uint32_t numViews = 0;            // 1..kMaxViews
};

struct MaskStats {
    uint32_t hexagons[3] = {};        // drawn per pass (seen, visible, unexplored), after the 2,048-per-pass cap
    uint32_t dropped = 0;             // hexagons over the cap
    uint32_t draws = 0;
};

static constexpr uint32_t kHexagonsPerDraw = 24;      // FOWDrawMask's cb0: 24 float4x4 (0x600 bytes)
static constexpr uint32_t kMaxHexagonsPerPass = 2048; // stock's cap per pass over all views; more are dropped
static constexpr int32_t kMaxRangeSpan = 4096;        // a view's range wider or taller than this is refused

class MaskRenderer {
public:
    MaskRenderer() = default;
    ~MaskRenderer();
    MaskRenderer(const MaskRenderer&) = delete;
    MaskRenderer& operator=(const MaskRenderer&) = delete;

    // The FOWDrawMask programs (passes 0-2) from the .shader section of exePath (read-only), the two hexagon sets, the
    // MAX blend, and the mask target of size x size (1024 for FOWLevel 2-3, 512 for 0-1): R8G8B8A8_TYPELESS with the
    // full mip chain (11 / 10 levels), UNORM views, GENERATE_MIPS, as stock's (fog-of-war.md 2.1, 7.1).
    // Errors: BadLayout (dev null, size not 512 or 1024), Io / BadMagic / Truncated (the EXE file), NotFound (a
    // FOWDrawMask blob missing from the .shader section), Device / OutOfMemory. A failed init leaves the object empty.
    Status init(ID3D11Device* dev, const std::wstring& exePath, uint32_t size);
    // Clears the target to 0, draws every view's plots in three passes (seen with program pass 2 and hexagon set 0,
    // visible with pass 1 and set 0, unexplored with pass 0 and set 1; 24 hexagons a draw, at most 2,048 a pass), then
    // GenerateMips. The input is checked first; on any error nothing is drawn and the target keeps its contents.
    // Errors: NotFound (init not done), BadLayout (ctx null; states null, width or height 0 or above 4096, stateCount
    // != width * height, a state above 2; placement missing or not finite; views null, numViews 0 or above
    // kMaxViews; an OverlayProj not finite or with a zero x or y scale; a range wider or taller than kMaxRangeSpan).
    // Pipeline state: leaves IA, VS, PS, OM (no target bound), RS and the viewport changed and HS/DS/GS unbound; the
    // caller sets its own before its next draw. Never throws.
    Status build(ID3D11DeviceContext* ctx, const MaskInput& in, MaskStats* stats = nullptr);
    ID3D11ShaderResourceView* srv() const;   // the mask for terrain t4 and water/river t10 (null before init)
    uint32_t size() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

// ---- The cloud pass (task 3955; fog-of-war.md 7.2 "The cloud planes as built") ----
//
// What it draws, as stock does after the world (fog-of-war.md 2.3, 3.5, 7.2; [L] = read from 3951's captures):
//   1. FOW CULL (FOWDrawMask pass 3): per view, for every plot of the view's cull range whose cull flag is 1
//      (cullFlags()), a 64 x 55.4 quad (scale 32 sx, 32 sy) centred at HexToWorld(plot) + (32 sx, 32 sy - 128) at height
//      102 [L], its VS forcing depth 0; colour write off, depth LESS_EQUAL with write: the cloud planes fail the depth test
//      behind it. 24 quads per draw, at most 2,048 per view (more are dropped, as stock).
//   2. three planes (FOWBaseLayer pass 0): per view, one quad per 64-sample block of the view's footprint (the water's
//      footprint rows before the world-box clamp, planeBlocks(); at most 2,048 a view), at heights 11.2, 38.25, 51 [L], with
//      t0 = fogcloudtile_dif_02, _03, _01 and FOWAnim's zw scaled by 1.0, 1.5, 2.0 [L]; t1 = the mask (the EXE's PS
//      discards where mask.y < 0.01, alpha = mask.y x cloud alpha); blend SRC_ALPHA / INV_SRC_ALPHA, depth LESS_EQUAL
//      without write. Plane by plane, every view within a plane.
//   Rasterizer (both): solid, cull FRONT with FrontCounterClockwise, depth clip; samplers linear, wrap.

// The map's placement the cloud pass reads (3955): HexToWorld for the FOW CULL quads, the world box and the sample grid
// for the planes' blocks. The server has every value in its height field (heightfield::HeightGrid: hexToWorld, sx, sy,
// ox, oy, samplesX, samplesY) and water::worldFromGrid (the box).
struct CloudWorld {
    float hexToWorld[16] = {};       // M_h2w (0xD91030), row-major as the EXE stores it: (hx, hy, 0, 1) x M -> samples
    float sx = 1.0f, sy = 0.866f;    // HexScale (0xB9A6D4, 0xB9A6D8)
    float ox = 0.0f, oy = 0.0f;      // HexToWorld's offsets (GridHeightmap +0xB4, +0xB8)
    float minX = 0, minY = 0, maxX = 0, maxY = 0;   // the world box (0xD90FCC..0xD90FDC)
    uint32_t samplesX = 0, samplesY = 0;            // GridHeightmap +0xC, +0x10
};

struct CloudInput {
    ID3D11RenderTargetView* colour = nullptr;   // the server's scene colour after its world draws
    ID3D11DepthStencilView* depth = nullptr;    // the scene's depth, cleared to 1 and drawn LESS_EQUAL (FOW CULL writes 0)
    uint32_t width = 0, height = 0;             // the scene viewport
    ID3D11ShaderResourceView* mask = nullptr;   // MaskRenderer::srv() of this frame
    MaskInput geometry;                         // the same map and views as the mask (View::viewProj is needed here)
    float fowAnim[4] = {};                      // fowAnim() of this frame
    CloudWorld world;                           // (3955)
};

struct CloudStats {                             // (3955)
    uint32_t cullQuads = 0;                     // FOW CULL quads drawn, all views
    uint32_t cullDropped = 0;                   // over the 2,048-per-view cap
    uint32_t planeQuads[3] = {};                // per plane, all views
    uint32_t draws = 0;
};

// The FOW CULL flags of the EXE's FluidFOW update (fog-of-war.md 3.2, 7.2) [C]: out[y * width + x] = 1 when none of the
// 21 plots (x + dx, y + dy), dx, dy in [-2, 2] without the four corners (|dx| = |dy| = 2), indices wrapped modulo the
// map in both directions (whatever MapInput::wrapX says, as stock), is unexplored, and 0 in rows 0, 1 and height - 1.
// BadLayout: states null, or width or height 0.
Status cullFlags(const MapInput& map, std::vector<uint8_t>& out);

// HexToWorld's x, y of plot (gx, gy) without the height (camera-and-picking.md 5): hex (gx - (gy >> 1), gy, 0, 1) x
// M_h2w, then x sx - ox, y sy - oy.
void hexToWorldXY(const CloudWorld& w, int32_t gx, int32_t gy, float out[2]);

// The 64-sample blocks [bx0, bx1) x [by0, by1) of a view's footprint, as stock's plane draw takes them (fog-of-war.md
// 7.2, corrected by 0572 [C]): x from the footprint clamped to the world box (0xDA1B00 / 0xDA1B30), y from the footprint
// before the clamp (0xDA1B60 / 0xDA1B90), each corner rounded as the ocean's (water-system.md 10.2 step 2) and divided
// by 64 toward zero, by1 + 1 more than the ocean's (one row more at the high-y end), no block index clamped: at the
// south and north map edges the rows go on past the world box (by0 below 0). out = {bx0, bx1, by0, by1}. BadLayout: a
// matrix or the world not finite or not valid. The draw keeps at most 2,048 quads a view and plane (bx outer).
Status planeBlocks(const float viewProj[16], const CloudWorld& w, int32_t out[4]);

class CloudRenderer {
public:
    CloudRenderer() = default;
    ~CloudRenderer();
    CloudRenderer(const CloudRenderer&) = delete;
    CloudRenderer& operator=(const CloudRenderer&) = delete;

    // The FOWBaseLayer program and FOWDrawMask pass 3 from exePath; fogcloudtile_dif_01/02/03.dds from gameDir through
    // c5r::gamefs (EffectTextures.fpk). A missing file or program is a failure (design 5.5: D125's route).
    // Errors: BadLayout (no device), Io (the EXE or the install not readable), NotFound (a program, a set description or
    // a cloud file missing), the DDS parser's errors, Device / OutOfMemory (a create call).
    Status init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir);
    // FOW CULL then the three planes, into in.colour / in.depth. Sets its own pipeline state (input layout, buffers,
    // shaders, states, viewport, targets) and unbinds its two pixel-shader textures at the end; restores nothing else.
    // Errors: NotFound (not initialised), BadLayout (no context, target, depth or mask; viewport 0; views 0 or above
    // kMaxViews or null; map states null or empty; a matrix, FOWAnim or the world not finite or not valid), Device.
    // The cull range of a view is [x0, x1 + 1) x [y0, y1 + 2) of View's range (stock's loop over the same globals as the
    // mask's [x0, x1) x [y0, y1)); when x1 <= x0 it is every plot whose cull quad centre lies within the view's
    // footprint widened by 64 x 128 world units (an approximation until 3952 fits stock's range; fog-of-war.md 6).
    Status draw(ID3D11DeviceContext* ctx, const CloudInput& in, CloudStats* stats = nullptr);
    // The cloud base for terrain t5: fogcloudtile_dif_02.dds (fog-of-war.md 2.4). Null before init.
    ID3D11ShaderResourceView* cloudBase() const;
    // The plane textures in draw order (0: _02, 1: _03, 2: _01). Null before init or for i > 2.
    ID3D11ShaderResourceView* planeTexture(uint32_t i) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

// ---- The FOWLevel-3 particles (ticket 0340; fog-of-war.md 10) ----
//
// A re-implementation of the behaviour of the EXE's FluidParticles (fog-of-war.md 10.1, from a static description; D83):
// particles spawned on a lattice over the map at a game's start, an SPH-like repulsion with the flow field
// flowfield.dds, moved one step per drawn frame inside the area around view 0 (they circulate there), drawn as flat
// rotated sprites of the atlas fx_fog_dif.dds with the EXE's FOWParticleShader pass 0, gated by the mask's G. The random
// numbers come from the C runtime's rand() formula with the caller's seed, and the step does not read a clock, so the
// same seed and the same footprints give the same particles (a run is repeatable). Stock's own rand() sequence and its
// history are not reproducible: the picture is a look-alike, measured statistically (fog-of-war.md 10.3).

// FluidFOWSettings.xml's values; the defaults are the EXE's when an element is missing (fog-of-war.md 10.1).
struct ParticleSettings {
    int32_t spawnFrequency = 20;
    float sizeMean = 1.0f, sizeMin = 0.25f, sizeMax = 4.0f, sizeDeviation = 1.0f;
    float massMean = 1.0f, massMin = 0.125f, massMax = 8.0f, massDeviation = 1.0f;
    float massSizeCorrelation = 0.0f, fogHeight = 0.0f, heightVariation = 60.0f;
    float fluidDensity = 50.0f, speed = 5.0f, drag = 0.001f, masterSize = 1.0f, forceStrength = 1.0e-5f, maxRotate = 0.01f;
};

// Reads FluidFOWSettings.xml as the EXE does: children of the root found by name, each value read as a C++ stream reads
// a number (".5f" is 0.5); a value that does not read stops its group (group 1: SpawnFrequency .. HeightVariation,
// group 2: FluidDensity .. MaxRotate), whose remaining values keep their defaults. `note` lists what was not read.
// Errors: BadLayout (not XML, no root).
Status parseParticleSettings(const char* xml, size_t size, ParticleSettings& out, std::string* note = nullptr);

// flowfield.dds as the simulation reads it: per texel the first two channels as floats. An uncompressed 32-bit RGBA or
// BGRA file is read as R / 255 and G / 255 [I: fog-of-war.md 10.1 "Not traced"]. Errors: the DDS parser's, Unsupported
// (another format), BadLayout (empty).
struct FlowField {
    uint32_t width = 0, height = 0;
    std::vector<float> xy;   // 2 floats per texel, row-major
};
Status flowFieldFromDds(const uint8_t* data, size_t size, FlowField& out);

struct Particle {
    float x = 0, y = 0, z = 0;    // world
    float vx = 0, vy = 0;         // per step
    float mass = 0, density = 0, pressure = 0;   // pressure = mass / (2 FluidDensity density) of the last step
    float size = 0, rot = 0;
    uint32_t id = 0;              // n * 256 + atlas frame
};

// One sprite as the draw writes it: four vertices (POSITION float3, NORMAL float3 (0), TEXCOORD float2) and its sort key
// (the centre's clip w in the emitting view).
struct ParticleSprite {
    float v[4][8];
    float key;
};

struct ParticleStats {
    uint32_t particles = 0;       // alive in the grid
    uint32_t activeCells = 0;     // cells of the last step's rectangle
    uint32_t moved = 0;           // particles moved by the last step
    uint32_t lost = 0;            // dropped by the cell capacity (32) since reset
    uint32_t steps = 0;           // since reset
    uint32_t sprites = 0;         // the last emit (all views)
    uint32_t spritesDropped = 0;  // over the 10,240 cap in the last emit
};

static constexpr uint32_t kParticleCellCap = 32;       // particles a cell holds (stock's staging per cell)
static constexpr uint32_t kMaxParticleSprites = 10240; // stock's vertex buffer: 40,960 vertices
static constexpr int32_t kParticleCellSize = 64;       // world units (shift 6)
static constexpr int32_t kParticleMargin = 3;          // cells around the footprint that take part in a step

// The C runtime's rand(): state = state * 214013 + 2531011, result (state >> 16) & 0x7FFF.
struct CrtRand {
    uint32_t state = 1;
    int next() { state = state * 214013u + 2531011u; return static_cast<int>((state >> 16) & 0x7FFFu); }
};

static constexpr uint32_t kMaxStepThreads = 16;        // ParticleSim::setStepThreads' cap (ticket 0692)

class ParticleSim {
public:
    ParticleSim();
    ~ParticleSim();
    ParticleSim(const ParticleSim&) = delete;
    ParticleSim& operator=(const ParticleSim&) = delete;
    // The step's threads (ticket 0692 cut A): 1 = the calling thread alone (a new sim's count); n > 1 = the calling
    // thread and n - 1 helper threads of the sim's own, started at the first step that needs them, the passes split into
    // bands of cell rows. The state after a step is byte-identical for every n (fog-of-war.md 12.5). n is clamped to
    // 1..kMaxStepThreads. ParticleRenderer uses defaultStepThreads(): one for every 4 logical processors, 1 to 4.
    void setStepThreads(uint32_t n);
    uint32_t stepThreads() const { return m_threads; }
    static uint32_t defaultStepThreads();
    // A new game: the cell grid over the spawn box of a world of worldWidth x worldHeight (centred at 0) and the lattice
    // spawn (fog-of-war.md 10.1). flow is copied. atlasFrames = the sprite atlas's frame count (64 for 512 x 512).
    // Errors: BadLayout (a size not finite or not > 0, spawnFrequency < 1, an empty flow field, atlasFrames 0, a grid
    // above 4096 cells a side).
    Status reset(float worldWidth, float worldHeight, const ParticleSettings& s, const FlowField& flow, uint32_t seed,
                 uint32_t atlasFrames = 64);
    // One step of the area around the footprint [fpMin, fpMax] (view 0's ground footprint, world units).
    // Errors: NotFound (no reset), BadLayout (a value not finite).
    Status step(const float fpMin[2], const float fpMax[2]);
    // The sprites of the cells of a view's footprint, appended to out (at most kMaxParticleSprites in out); updates the
    // rotation of each emitted particle toward its velocity (MaxRotate per call), as stock's draw does.
    // atlasPerRow / du / dv: the atlas's frames per row and one frame's uv size. Errors: NotFound, BadLayout.
    Status emit(const float viewProj[16], const float fpMin[2], const float fpMax[2], uint32_t atlasPerRow, float du, float dv,
                std::vector<ParticleSprite>& out);
    // Back to front by key (descending; equal keys keep their emission order).
    static void sortSprites(std::vector<ParticleSprite>& s);
    // The same order without moving the sprites (ticket 0692 cut C; what ParticleRenderer::frame uses): order gets one
    // (key, index) pair a sprite, stable-sorted by the same comparison, so s[order[i].second] is sortSprites' i-th sprite.
    static void sortOrder(const std::vector<ParticleSprite>& s, std::vector<std::pair<float, uint32_t>>& order);

    // The whole state, byte-exact (a recorded span's start; fog-of-war.md 10.4): settings, flow field size and a hash of
    // it (the field itself is the install's), the grid, the generator, every particle. loadState refuses (BadLayout /
    // BadMagic / Truncated) anything else, and a flow field whose hash differs from the one loaded with reset.
    std::vector<uint8_t> saveState() const;
    Status loadState(const uint8_t* data, size_t size);

    bool ready() const { return m_ready; }
    const ParticleStats& stats() const { return m_stats; }
    const ParticleSettings& settings() const { return m_set; }
    int32_t cellsX() const { return m_cx; }
    int32_t cellsY() const { return m_cy; }
    float originX() const { return m_gx; }
    float originY() const { return m_gy; }
    const std::vector<Particle>& cell(int32_t x, int32_t y) const { return m_cells[static_cast<size_t>(y) * m_cx + x]; }
    // the cell of a world point as stock maps it (clamped into the grid)
    void cellOf(float x, float y, int32_t& cx, int32_t& cy) const;
    // every particle (tests, statistics)
    void forEach(void (*fn)(const Particle&, void*), void* ctx) const;

private:
    struct StepPool;
    void bin(const Particle& p, std::vector<std::vector<Particle>>& cells);
    std::unique_ptr<StepPool> m_pool;   // the step's helpers and band lists (0692 cut A)
    uint32_t m_threads = 1;
    bool m_ready = false;
    ParticleSettings m_set;
    FlowField m_flow;
    uint64_t m_flowHash = 0;
    uint32_t m_frames = 64;
    int32_t m_cx = 0, m_cy = 0;
    float m_gx = 0, m_gy = 0;     // grid origin (the spawn box's min minus one cell)
    CrtRand m_rand;
    std::vector<std::vector<Particle>> m_cells;
    ParticleStats m_stats;
};

struct ParticleInput {
    ID3D11RenderTargetView* colour = nullptr;   // the scene's colour after the cloud planes
    ID3D11DepthStencilView* depth = nullptr;    // the cloud pass's depth (FOW CULL's zeros hide the particles)
    uint32_t width = 0, height = 0;
    ID3D11ShaderResourceView* mask = nullptr;   // MaskRenderer::srv()
    const View* views = nullptr;                // viewProj and overlayProj per view
    uint32_t numViews = 0;
    CloudWorld world;                           // the world box and the sample grid (the footprint)
};

class ParticleRenderer {
public:
    ParticleRenderer() = default;
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // FOWParticleShader pass 0 from exePath; fx_fog_dif.dds, flowfield.dds and FluidFOWSettings.xml from gameDir through
    // c5r::gamefs. Errors: BadLayout (no device), Io, NotFound (a program or a file), the parsers', Device / OutOfMemory.
    Status init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir);
    // A new game (ParticleSim::reset with the install's settings and flow field). Errors: NotFound (no init), reset's.
    Status reset(float worldWidth, float worldHeight, uint32_t seed);
    // One drawn frame: the sprites of every view (emit, sort), one draw per view with that view's matrix and OverlayProj,
    // then one step for the next frame (view 0's footprint). Sets its own pipeline state; unbinds its two PS textures.
    // Errors: NotFound (no init or reset), BadLayout (no context, target, depth or mask, viewport 0, views 0 or above
    // kMaxViews, a matrix or the world not finite), Device. On an error nothing is stepped.
    Status frame(ID3D11DeviceContext* ctx, const ParticleInput& in, ParticleStats* stats = nullptr);
    ParticleSim& sim();
    const ParticleSettings& settings() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace fow
} // namespace c5r
