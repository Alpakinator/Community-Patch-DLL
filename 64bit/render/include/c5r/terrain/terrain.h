// c5r/terrain/terrain.h - the terrain surface drawn as the EXE draws it (epic 3200, seam A of package I2-E).
//
// First version committed by the team-3200 lead as the contract between the draw module (issue 3202, owner of this file
// from then on: team-3200 W1) and its users: render/tools/terrainshot (offline, from the ground truth) and
// shell_server.exe --scene terrain (issue 3205). Facts it rests on: research/05-graphics-composition/asset-pipeline.md
// 3.3 (TerrainFrame, TerrainTile, DistanceFog), 3.4 (textures and samplers), 3.6 (the tessellated path needs no vertex
// buffer) and 4.2 point 5 (the height field and its world relation); the draw itself, the tile layout and the premises
// P11, P16, P17: research/05-graphics-composition/terrain-surface.md.
//
// What the module does: from a tapped height field (c5r::heightfield::HeightGrid) it builds the EXE's height pages (one
// height texture per terrain tile until issue 0191), and draws every tile with the EXE's own TerrainShader programs (material 73, tessellated passes 0-2:
// VS 0x067d6490, HS 0x067f98f0 / 0x067f9cb8, DS 0x067fa080, PS by pass; taken from CivilizationV_DX11.exe through
// c5r::shaders / c5r::terrainsets) with TerrainFrame, TerrainTile and DistanceFog filled from the camera. The patch list
// is 16 control points per height sample (SV_VertexID = i + 256 * j, the EXE VS's scheme), so the surface is the EXE's
// bicubic patch surface over the samples. Colour comes from the tapped tile caches (decision D58 point 1a) when a tile
// has them, else from the fallback named in Options (1c: the module's own pixel shader by material byte; or a
// constant). Water is a flat placeholder plane at the grid's waterZ in one colour (D58 point 7). Inputs the EXE takes
// from other systems are constants (overlay transparent, FOW visible, shadow white, identity LUTs: inventory 2.6, seam
// G). Distance fog: the EXE's DistanceFog block from the camera and the install's fog ramp (issue 0188; before it,
// FogParams 0).
//
// Tiles: a tile is tilePlots x tilePlots plots (tilePlots * 64 height samples a side, one patch per sample). The EXE's
// own tile extent is premise P16 (terrain-surface.md 2): until it is known live, tilePlots is a parameter; the tapped
// colour caches are only placed right when it equals the EXE's.
//
// Conventions: matrices are D3DX row-major, row-vector convention (clip = (x, y, z, 1) * view * proj), exactly as the
// camera block of shell/common/shell_shared.h (VPShellCamera) and the ground-truth camera JSON hold them. World units
// and axes are the EXE's (asset-pipeline.md 4.2 point 5: x = c*sx - ox, y = r*sy - oy, z = u16/255*sz).
// Errors: every call returns a c5r::Status; nothing throws; bad input is a named Err, never a crash.
//
// API changes against the lead's first version (2026-11-11, W1): setHeightField takes the tile size (tilePlots,
// default kDefaultTilePlots); TileGrid adds the tile's sample count; ColorSource adds WorldPos (float target: world
// xyz, for the offline comparison); Options adds lodMip, tileD4 (the cache uv) and clearDepth; FrameStats adds
// tessFactorUsed (tilesDrawn counts wrap copies too); new gpuTime() reads the GPU timestamp of the last draw (optionally waiting). Everything
// the lead's version had is kept with the same meaning.
// Issue 3704 (epic 0167, 2026-11-14, 0167-W4): Options::overlay (t1 and OverlayProj per wrap copy), overlayProjForCopy,
// FrameStats::overlayBound; the default keeps the transparent 1x1 and identity, bit-equal to the picture before.
// Issue 0188 (2026-11-14, impl-B-0188): DistanceFog, distanceFogBlock, fogZoomFraction, Options::fog,
// FrameStats::fogOn, Renderer::fogRamp; init reads fogramp.dds from gameDir (empty gameDir: no ramp, no visible fog,
// bit-equal to the picture before).
// Issue 0191 (2026-11-14, impl-B-0191): the EXE's LOD. The height textures are the EXE's 1024 x 1024 A8 pages with all
// 11 mips (c5r::bake::buildHeightPage) instead of one mip-0 texture per tile; Options::lodLevel (default kLodExe: L per
// frame by the EXE's rule with its hysteresis, LodMip = L, LodStep = 2^L, the tile's patch grid thinned, TessFactor as the
// EXE) replaces Options::lodMip; Options::lodWidth, lodTriSize, lodSubdiv; FrameStats::lod*; the free functions of the
// rule (exeTileBox .. exeFrameLod) and Renderer::lodState / resetLod. research/05-graphics-composition/terrain-surface.md 7.
// Issue 0189 (2026-11-14, impl-B-0189): the EXE's pass per tile. Options::pass -1 now draws a tile that has a spec cache
// t8 (TileCache::second; the EXE's tile+0x2FC) with pass 0 (PS 0x067e6e58: t0 plus t8 * t9 in linear light) and the others
// with pass 1, as 0x5C37E0 does; t9 is the install's `specscreenmap.dds` (init reads it from gameDir with the fog ramp);
// TerrainFrame.FlagB (cb1[27], the PS's spec switch) = Options::spec and a spec map bound; FrameStats::tilesPass0 / 1,
// specOn, listPass0 / listPass1 / listNoCache / listPass0Hash (over the EXE's visible list); Renderer::specMap. Without an install FlagB is 0 and a pass-0 tile is bit-equal to its pass-1 picture.
// research/05-graphics-composition/terrain-surface.md 8.
// Issue 3953 (epic 0168, 2026-11-14, impl-3953): the fog of war's inputs (research/05-graphics-composition/fog-of-war.md
// 3.4, 3.6, 3.10, 7.4). Options::fow (t4 the FOW mask, t5 the cloud base, FOWAnim in TerrainFrame 0x090, and t6 the
// TerrainFOW LUT of each tile's style); the tile style (setTileStyle, setTileStyles, tileStyleAt); init reads the five
// `TerrainFOW_<Style>_ColorKey.tga` from gameDir (a missing file keeps the identity volume, as the EXE's loader does);
// setFowLut, fowLut, fowLutsFromInstall; FrameStats::fowOn, fowStyleTiles. Off (the default): t4 white, t5 black, t6
// identity, FOWAnim 0, bit-equal to the picture before.
// Issue 0190 (2026-11-14, impl-B-0190): the models' shadows on the ground. Options::shadow (t3, ShadowMatrix0 per wrap
// copy, Arg6), shadowMatrixForCopy; init reads `TerrainShadow_Generic_ColorKey.tga` as t7 (bound only with a shadow);
// FrameStats::shadowOn / shadowLutFromFile; Renderer::shadowLut / shadowLutFromFile. Off (the default): bit-equal to the
// picture before. research/05-graphics-composition/terrain-surface.md 9.
// Issue 4305 (epic 0169, 2026-11-15): t7 per tile style, as t6: init reads the five `TerrainShadow_<Style>_ColorKey.tga`
// (missing = identity, unparsable = init error naming it); shadowLut(style), shadowLutsFromFile, setShadowLut;
// FrameStats::shadowStyleTiles. terrain-surface.md 12.6.
// Ticket 4551 (epic 0431, 2026-11-16): drawScreenOverlay, the EXE's Screen Overlay pass (pass 2 into a frame-size
// target, the trees' t3); draw() unchanged. research/05-graphics-composition/trees.md 11.2.
// Issue 0198 (2026-11-15, impl-B-0198): init(dev, exe, const gamefs::GameFiles&) reads the fog ramp, the spec map and the
// TerrainShadow ColorKey from an index the caller already opened (the server's scene shares one), so the init does not
// build the install's index again (1.3-1.5 s, terrain-surface.md 6); init(dev, exe, gameDir) opens its own as before.
// Ticket 0665 (2026-11-22): with ColorSource::Caches and fallback Material (the product's default) a tile that is not land
// and has no cache (open sea, the EXE's height class 1) is not drawn, as the EXE draws none; FrameStats::seaTilesHidden.
// The other fallbacks (Constant, Height, Normal, WorldPos) still draw it. fog-after-camera-jump.md 6.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/bake/heightpage.h"   // ticket 4909: bake::HeightPage / HeightPageIndex in updateHeightRegion

namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace terrain {

// The camera of one frame (VPShellCamera fields of the same names).
struct Camera {
    float view[16] = {0};      // g_View[0]
    float proj[16] = {0};      // g_Proj
    float camWorld[16] = {0};  // FCamera camera-to-world; row 3 = position (all zero: computed from view)
    uint32_t width = 0, height = 0;  // render-target pixels (the EXE's back buffer, e.g. 2554 x 1411)
};

enum class ColorSource {
    Caches,    // decision 1a: the tile caches set with setTileCache (tiles without caches use `fallback`; with fallback
               // Material a non-land tile without a cache is not drawn, ticket 0665)
    Material,  // decision 1c: own PS, colour by the field's material byte (constant colour when the field has none)
    Constant,  // one colour (Options::constantRgba), geometry checks
    Height,    // debug: height ramp
    Normal,    // debug: shading by the surface normal (screen-space derivative of the world position)
    WorldPos,  // debug: world x, y, z, 1 into a float target (R32G32B32A32_FLOAT), for the offline comparison
};

// The height the EXE's terrain draw gives sample (x, y) (issue 0134, 2026-11-13): the EXE's VS reads the tile's 1024 x
// 1024 A8_UNORM height page, whose texel is the HIGH BYTE of the u16 sample (terrain-bake-passes.md 5.0 [C + L: captured
// pages equal the rule]), as z = alpha * HexScale.z * 255 with HexScale.z = 0.2 (5.3), so z = (u16 >> 8) * sz. That is
// up to 0.25 world units BELOW HeightAtGrid 0x6D8B60's u16 / 255 * sz (where the EXE places models and units); the
// module draws this height, not HeightAtGrid's. Wrap and the outside-is-0 rule are HeightAtGrid's, as in
// heightfield::heightAtSample (the page itself wraps in both axes; no tile here reaches outside in y but the border).
float drawnHeightAtSample(const heightfield::HeightGrid& g, int x, int y);

const uint32_t kDefaultTilePlots = 2;  // until P16 is known live; 1..3 allowed (vertex-ID limit: 256 per row)
const uint32_t kMaxTilePlots = 3;
const uint32_t kTileBorder = 3;        // height samples kept around a tile (the EXE VS's "-3" in i - 3)

// Epic 0465, ticket 4909: a rectangle of height samples, [x0, x1) x [y0, y1), inside the grid (no wrap: a window that
// crosses the x seam is two rects).
struct SampleRect {
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// The EXE height pages (bake::buildHeightPage's index, 1024-sample window from p * 896 - 64, both axes wrapping) whose
// window holds a sample of a rect, in (py, px) order. Errors: BadLayout (heightPageCount's, a rect empty or outside).
Status pagesForRects(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects,
                     std::vector<bake::HeightPageIndex>& out);
// True when [lo, hi) (a rect's span) holds a sample of the window [w0, w0 + n) taken modulo period (both may wrap).
bool spanMeetsWrappedWindow(int64_t lo, int64_t hi, int64_t w0, int64_t n, int64_t period);
// Folds every level of a 2D texture view (read back through a staging copy, on the device's immediate context) into
// the FNV-1a 64 h; a null view folds "null". Tests and the bench only (ticket 4909). Errors: BadLayout, Device.
Status digestTexture(ID3D11Device* dev, ID3D11ShaderResourceView* srv, uint64_t& h);

// What updateHeightRegion did (ticket 4909's timers and counts).
struct RegionUpdateStats {
    uint32_t pagesRebuilt = 0, pagesFromWorker = 0, tilesBounds = 0, tilesMat = 0, tilesLand = 0, exeTiles = 0;
    double buildMs = 0, uploadMs = 0, tilesMs = 0, totalMs = 0;
};

// The Terrain Overlay input (epic 0167, issue 3704; research/05-graphics-composition/overlay-system.md 3-5): TerrainShader
// t1 and TerrainFrame.OverlayProj. The PS composes it premultiplied, colour * (1 - t1.a) + t1.rgb, before the FOW and the
// distance fog; only draws through the EXE's pixel shader (tiles with a cache) show it, as in the EXE.
// proj[k + 1] is the OverlayProj of the EXE view that shows wrap copy k (k = -1, 0, +1; 0 = the camera's own view) as the
// EXE stores it (0xDA1690 + 0x40 v, row-major), i.e. applied to the world position of the EXE's own, unshifted tile. The
// module draws its wrap copies with TileWorld x shifted by k * wrapPeriod (the EXE's period, (samplesX - 1) * sx = 3583 on
// Tiny; issue 4314: it was samplesX * sx = 3584 until 2026-11-15, one world unit off stock), so it binds
// Translation(-k * wrapPeriod) * proj[k + 1] for copy k: the copy samples the texels the EXE's view sampled for that ground.
// The caller never derives a wrap view's matrix from view 0's when the EXE has that view (overlay-system.md 4).
// srv null (the default): the module's 1x1 (0, 0, 0, 0) and identity matrices, i.e. the picture before 3704, bit-equal.
struct Overlay {
    ID3D11ShaderResourceView* srv = nullptr;   // Texture2D, R8G8B8A8_UNORM (the EXE's view of its target), any size and mips
    float proj[3][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                         {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                         {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
};

// The matrix the module binds for wrap copy k (-1, 0, +1) when drawing with o: Translation(-k * shift) * o.proj[k + 1]
// (row-vector convention), shift = wrapPeriod of the field; identity when o.srv is null. For tests and terrainshot.
void overlayProjForCopy(const Overlay& o, int k, float shift, float out[16]);

// The EXE's wrap period in world units, the x shift between the terrain's wrap copies (issue 4314; terrain-surface.md
// 13.8): MapSetup 0x535BF0 stores float(64 * mapW - 1) at 0xD90FE4 for a game's map (wrapping or not) [C], and the world
// box 0xD90FCC..0xD90FD8 is -+0.5 of it; the EXE's wrap views are view 0 moved by it (3582.99975 on Tiny [L]). With the
// EXE's 64 samples per plot and sx 1 it is (samplesX - 1) * sx, bit-equal to the EXE's float (3583 Tiny, 8191 Huge), one
// unit less than the field's sample span samplesX * sx: the copies overlap by one sample column, as the EXE's. The same
// value as water::worldFromGrid(g).width. 0 when samplesX is 0.
float wrapPeriod(uint32_t samplesX, float sx);

// The models' shadows on the ground (issue 0190; research/05-graphics-composition/terrain-surface.md 9): TerrainShader t3
// (the EXE's ShadowBlur target 0xDA12BC, 2048 x 2048 R16_UNORM, drawn every frame by the EXE's shadow pass 0x523CA0 from
// the units, cities and landmarks into ShadowRaw and resampled), TerrainFrame.ShadowMatrix0 (0x1F0; the EXE's 0xDA1790 +
// 0x40 v) and TerrainFrame.Arg6 (0x190; the EXE's eased shadow factor [0xD90FA8]). The PS takes s = saturate(t3((world, 1)
// * ShadowMatrix0).xy) * Arg6 and lerps the colour toward LUT t7 (colour) by s, before the overlay, the FOW and the fog;
// t7 is the install's TerrainShadow ColorKey volume (init). matrix[k + 1] is the EXE view's matrix for wrap copy k, as
// stored (applied to the EXE's own, unshifted tile); the module binds shadowMatrixForCopy (Overlay's rule).
// srv null (the default): Arg6 0, t3 white, ShadowMatrix0 identity, t7 identity: the picture before 0190, bit-equal.
struct Shadow {
    // Texture2D R16_UNORM view (the EXE's ShadowBlur copy, quality 2-3), any size, 1+ mips; issue 4308: or
    // R16G16B16A16_FLOAT (quality 1: ShadowBlur is the back-buffer-sized ShadowRaw; the PS reads .r)
    ID3D11ShaderResourceView* srv = nullptr;
    float strength = 1.0f;                     // Arg6 while srv is bound: [0xD90FA8] of the same frame; finite, [0, 1]
    float matrix[3][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                           {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                           {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
};

// The ShadowMatrix0 the module binds for wrap copy k (-1, 0, +1): Translation(-k * shift) * s.matrix[k + 1], shift =
// wrapPeriod of the field; identity when s.srv is null. For tests and the server.
void shadowMatrixForCopy(const Shadow& s, int k, float shift, float out[16]);

// The EXE's distance fog (issue 0188; research/05-graphics-composition/terrain-surface.md 6): TerrainShader cb2
// "DistanceFog" (shared buffer 19, 32 bytes) = FogColor, FogParams = (-density, exp(-density * camZ) * (-1 / density) *
// scale, 0, w), and t2 the fog ramp (`fogramp.dds`, 128 x 8). The PS mixes toward FogColor by
// (1 - min(exp(|d|^2 * FogParams.y * (1 - exp(d.z * FogParams.x)) / d.z), 1)) * ramp(0.5 + 0.5 * v5.y, w).x, d = the
// pixel's world position minus CameraPos. Defaults: the EXE's values [L, 3601's two stock captures, bit-equal]; w < 0 =
// fogZoomFraction(camera height) [I: the EXE writes its camera controller's target zoom, 0xD90FAC; equal at rest to a
// few 1e-5 at zoom 17, 0.0018 at zoom 11 where the EXE's is 0: the same ramp row].
struct DistanceFog {
    bool enabled = true;                       // false: FogParams 0, FogColor grey (the module before 0188)
    float color[4] = {146.0f * (1.0f / 255.0f), 182.0f * (1.0f / 255.0f), 243.0f * (1.0f / 255.0f), 1.0f};  // as the EXE
                                               // rounds it (c * (1/255) in float; 243 / 255 is one ulp lower)
    float density = 0.05f;
    float scale = 5.0e-5f;
    float w = -1.0f;                           // the ramp row; < 0: fogZoomFraction of the camera position's height
};

// The fog of war's inputs (issue 3953; fog-of-war.md 3.4, 3.6, 7.4). The EXE's terrain PSs (passes 0 and 1) read the
// mask t4 at the overlay's coordinate (TerrainFrame.OverlayProj, i.e. Options::overlay.proj per wrap copy: the mask and
// the overlay share the window, as in stock) and compose, after the overlay and before the distance fog:
//   colour = lerp(t5(cloud uv), colour, t4.x); colour = lerp(colour, t6(colour), t4.x * (1 - t4.z))
// with the cloud uv = (world.xy + FOWAnim.zw) * FOWAnim.xy (the DS). t4: R explored, G unexplored, B visible.
// enabled false (the default): t4 = (1, 1, 1, 1), t5 black, t6 identity, FOWAnim 0: the picture before 3953, bit-equal.
// enabled true: mask and cloudBase are required (null = BadLayout: the fog never draws the map without its mask, D118);
// t6 = fowLut(style of the tile) (identity for a style whose file the install lacks). The overlay matrices are used for
// the mask also when Options::overlay.srv is null (then the overlay itself stays transparent).
struct Fow {
    bool enabled = false;
    ID3D11ShaderResourceView* mask = nullptr;       // t4: c5r::fow::MaskRenderer::srv(); a Texture2D R8G8B8A8_UNORM view
    ID3D11ShaderResourceView* cloudBase = nullptr;  // t5: c5r::fow::CloudRenderer::cloudBase() (fogcloudtile_dif_02.dds);
                                                    // any Texture2D view
    float anim[4] = {0, 0, 0, 0};                   // TerrainFrame.FOWAnim: c5r::fow::fowAnim(worldWidth, seconds)
};

// The camera controller's zoom fraction as the water module computes it (c5r::water::zoomFraction, issue 3609):
// (clamp(ln(height / 50) * 6.6667, min, max) - min) / (max - min), clamped to [0, 1]; 0 for a height <= 0.
float fogZoomFraction(float cameraHeight, float zoomMin = 11.0f, float zoomMax = 20.5f);

// The DistanceFog block (8 floats: FogColor, FogParams) for a camera position. Disabled: (0.5, 0.5, 0.5, 1, 0, 0, 0, 0).
// FogParams.y with every step in double (bit-equal to the EXE at 3601's cameras); 0 when density is 0.
void distanceFogBlock(const DistanceFog& f, const float camPos[3], float out[8]);

// ---- The EXE's terrain LOD (issue 0191; research/05-graphics-composition/terrain-surface.md 7) ----
// Per frame the EXE takes the minimum M over the tiles of its main view's visible list of the tile LOD value (0x6497C0)
// = clamp(0.5 + log2((max(d4, d8) - 1) / max(1, p * 0.5 / TriSize / Subdiv)), -2, 4), p = the tile box's projected size
// (0x744AE0: 0.25 * back-buffer width * the largest screen-space length of the box's four space diagonals in NDC);
// then (0x4241E0) it accepts M against TR+0x242C / +0x2430 with a hysteresis of 0.25 per level change, and sets LodMip =
// floor(M), TessFactor = Subdiv (M >= 0) or LodMip 0, TessFactor (1 << int(M * -0.75)) * Subdiv (M < 0). A tile's level
// is L = int(LodMip): its vertex count n = d halved L times ((n >> 1) + 1), (n - 1)^2 patches, LodStep = (d - 1) / (n - 1).
const int kLodExe = -1;          // Options::lodLevel: L per frame by the EXE's rule (the default)
const int kMaxLodLevel = 4;      // the rule's upper clamp (0x6497C0)

// The EXE's box of a tile (tile+0x10C..+0x120, written at tile creation 0x5C37E0's caller): min = (originX * sx - ox,
// originY * sy - oy, 0), max = min + (d4 * sx, d8 * sy, 255 * sz), d4 / d8 = the tile's vertex counts (tile+0xD4 /
// +0xD8: 65 for a 64-sample tile, 64 in the last column / row of the map). box = {minX, minY, minZ, maxX, maxY, maxZ}.
void exeTileBox(uint32_t originX, uint32_t originY, uint32_t d4, uint32_t d8, float sx, float sy, float sz, float ox,
                float oy, float box[6]);
// The tile's vertex count along one axis as the EXE has it (tile+0xD4 / +0xD8): min(tileSamples + 1, samples - origin).
uint32_t exeTileVerts(uint32_t origin, uint32_t tileSamples, uint32_t samples);
// 0x744AE0: the box's projected size in pixels for viewProj (row-vector convention) and the back-buffer width.
float exeTileProjectedSize(const float box[6], const float viewProj[16], uint32_t backBufferWidth);
// 0x6497C0: the tile LOD value for a projected size p and the tile's max(d4, d8).
float exeTileLod(float projectedSize, uint32_t maxVerts, int triSize = 4, int subdiv = 2);
// The cull 0x7BA820 / 0x4AD810 for the main view's visible list: the box's bounding sphere (centre = half the sum of its
// corners, radius = half its diagonal) is visible unless it lies wholly outside one of the six planes of viewProj
// (left, right, bottom, top, near z >= 0, far), each normalised by its xyz length.
bool exeTileSphereVisible(const float box[6], const float viewProj[16]);

// TR+0x2424 .. +0x2430: the frame LOD state. Zero at terrain creation (0x678290), TessFactor = Subdiv.
struct LodState {
    float lodMip = 0.0f;     // TR+0x2424, floor of the accepted value (0 when it is negative)
    int tessFactor = 2;      // TR+0x2428
    float value = 0.0f;      // TR+0x242C, the accepted M
    float hyst = 0.0f;       // TR+0x2430, 0.25 * (floor(M) - floor(previous value)) at the last level change
};
// 0x4241E0's update with the frame's minimum M (call only when the visible list is not empty). Returns true when M was
// accepted (the state changed).
bool exeLodUpdate(LodState& s, float minLod, int subdiv = 2);
// The level's patch grid of a tile with d vertices (d = tileSamples + 1 here): patches per edge and LodStep.
void exeLodGrid(uint32_t d, int level, uint32_t* patches, float* lodStep);

// The cull lists only tiles of height class 3 (tile+0x328 from 0x7212B0; classes 1 and 2 are skipped): the highest u16
// sample of the window [origin + 4, origin + 67) in both axes (wrapping in both), divided by 255, is not below 30.0
// (terrain-bake-residency.md 1; the window's edge rule is [I]). For the EXE's 64-sample tile (tx, ty).
bool exeTileClass3(const heightfield::HeightGrid& g, uint32_t tx, uint32_t ty);

// The visible list and its minimum for one frame over a field's EXE tiles (64 samples, origins in steps of 64, the box
// above), main view only (no wrap copy): the tiles with listed[ty * tilesX + tx] != 0 (null: every tile; the module
// passes exeTileClass3) whose sphere is visible. minLod stays 10 (the EXE's start value) when the list is empty.
struct LodFrame {
    float minLod = 10.0f;
    uint32_t visible = 0;
    uint32_t minTile = 0;    // index ty * tilesX + tx of the tile that gave minLod (64-sample tiles)
};
Status exeFrameLod(uint32_t samplesX, uint32_t samplesY, float sx, float sy, float sz, float ox, float oy,
                   const float viewProj[16], uint32_t backBufferWidth, int triSize, int subdiv, LodFrame& out,
                   const uint8_t* listed = nullptr, std::vector<uint32_t>* visibleTiles = nullptr);

struct Options {
    // TerrainShader pass 0..2 for every tile; -1 = the EXE's choice per tile (0x5C37E0 at the owner's settings, issue 0189):
    // pass 0 for a tile whose cache has a t8 (TileCache::second, the EXE's tile+0x2FC), else pass 1
    int pass = -1;
    bool spec = true;            // issue 0189: TerrainFrame.FlagB (pass 0's spec term) = spec && the install's spec map bound
    float tessFactor = 0.0f;     // 0 = the EXE's value (Subdiv at L >= 0, more at L < 0); else this factor
    // Issue 0191: the LOD. kLodExe (default) = per frame by the EXE's rule and hysteresis (state kept by the Renderer);
    // 0..kMaxLodLevel = that level fixed (LodMip = L, LodStep = 2^L; the module before 0191 drew 0). Replaces lodMip.
    int lodLevel = kLodExe;
    uint32_t lodWidth = 0;       // the EXE's back-buffer width the rule scales by ([0x301056C]); 0 = the camera's width
    int lodTriSize = 4;          // BicubicTerrainTessTriSize (0xD90F18) at the owner's settings; 1..64
    int lodSubdiv = 2;           // BicubicTerrainTessSubdiv (0xD90F14) at the owner's settings; 1..64
    uint32_t tileD4 = 0;         // the EXE's tile+0xd4/+0xd8 for the cache uv (InvGridSize = 1/(d4 + 1)); 0 = P + 1 [I,
                                 // P16: set it from issue 3204's live value]; 1..1024
    ColorSource color = ColorSource::Caches;
    ColorSource fallback = ColorSource::Material;
    uint32_t constantRgba = 0xFF808080u;
    bool water = true;           // the flat placeholder plane at waterZ
    float waterZ = -1.0f;        // < 0: the grid's waterZ (0xD91274)
    uint32_t waterRgba = 0xFF6E4A2Au;  // R in the low byte
    bool wireframe = false;
    uint32_t clearRgba = 0;      // 0 = do not clear the target; else clear it to this colour first
    bool clearDepth = true;      // clear the depth buffer to 1 first (own depth buffer: always)
    // Ticket 0707: blocks of 8 x 8 tiles culled before the per-tile box test (same tiles drawn, same order, same counters
    // and picture: the block test keeps a margin above float rounding); false = the per-tile test alone (tests compare)
    bool coarseCull = true;
    Overlay overlay;            // issue 3704: t1 and OverlayProj per wrap copy (default: transparent, identity)
    DistanceFog fog;             // issue 0188: the EXE's distance fog (default on; visible only with the ramp of init)
    Fow fow;                     // issue 3953: the fog of war's t4 / t5 / t6 / FOWAnim (default off: bit-equal)
    Shadow shadow;               // issue 0190: t3, ShadowMatrix0 per wrap copy, Arg6 (default off: bit-equal)
    // Ticket 0753 (J3, record ahead): false = draw() issues and reads no GPU timestamp query (a draw recorded on a deferred
    // context, which cannot read one back); the caller times the list's execute with gpuTimingBegin / gpuTimingEnd
    bool gpuTiming = true;
};

struct FrameStats {
    uint32_t tilesDrawn = 0, tilesCulled = 0, tilesWithoutCache = 0;
    // ColorSource::Caches: LAND tiles (the EXE's height class 2-3, as the residency classes them) drawn without a cache,
    // each counted once per draw also when drawn in two wrap copies: the gray tiles of this frame, counted at the draw
    // itself and independent of the residency's counters (issue 0139). tilesWithoutCache also counts water tiles.
    uint32_t landTilesWithoutCache = 0;
    // ticket 0665: ColorSource::Caches with fallback Material: tiles in view that are not land and have no cache (the
    // EXE's height class 1, open sea), NOT drawn (as the EXE, whose list holds class 3 only); counted once per draw (wrap
    // copies not counted) and also in tilesWithoutCache. Other fallbacks draw them and leave this 0.
    uint32_t seaTilesHidden = 0;
    // Ticket 0707: tile visits the coarse cull skipped (every wrap copy), and FNV-1a 32 over (wrap copy + 1, tile index)
    // of each tile drawn, in draw order (the order the per-tile cull alone gives)
    uint32_t coarseSkipped = 0;
    uint32_t drawnOrderHash = 2166136261u;
    double cpuMs = 0;          // CPU time inside draw()
    double gpuMs = -1;           // GPU time of the previous frame's draw from timestamp queries; -1 = not available yet
    int passUsed = -1;           // Options::pass as resolved: 0..2 forced, -1 per tile (counts below)
    uint32_t tilesPass0 = 0, tilesPass1 = 0;   // issue 0189: draws of the EXE's PS by pass (wrap copies counted, as tilesDrawn)
    bool specOn = false;         // issue 0189: FlagB 1 (Options::spec and the spec map bound as t9)
    // issue 0189: the pass the module gives each tile of the EXE's visible list (lodVisible tiles: the ones the EXE draws,
    // 0191), drawn by the module's own cull or not; counted only with kLodExe, ColorSource::Caches and 1-plot tiles (a
    // module tile is an EXE tile). listPass0Hash: FNV-1a 32 over the pass-0 tiles' indices ty * tilesX + tx (4 bytes
    // little-endian each, in increasing index order), to compare the set with the EXE's tile+0x2FC tile for tile.
    bool listCounted = false;
    uint32_t listPass0 = 0, listPass1 = 0, listNoCache = 0;
    uint32_t listPass0Hash = 2166136261u;
    float tessFactorUsed = 0;
    bool overlayBound = false;   // issue 3704: Options::overlay.srv was bound as t1
    bool fogOn = false;          // issue 0188: Options::fog.enabled and the install's fog ramp bound as t2
    bool shadowOn = false;       // issue 0190: Options::shadow.srv bound as t3 with Arg6 = its strength
    bool shadowLutFromFile = false;   // issue 0190: t7 was the install's TerrainShadow LUT (else identity); since 4305:
                                      // Generic's (style 0) came from a file
    // issue 0191: the LOD of this draw
    int lodLevel = 0;            // L of every tile (int(LodMip))
    float lodMip = 0;            // TerrainTile LodMip
    float lodStep = 1;           // TerrainTile LodStep (both axes)
    float lodMin = 10.0f;        // kLodExe: this frame's minimum M over the visible list (10 when it is empty)
    float lodValue = 0;          // kLodExe: TR+0x242C after the update
    uint32_t lodVisible = 0;     // kLodExe: tiles in the main view's visible list
    bool lodAccepted = false;    // kLodExe: M was accepted by the hysteresis this frame
    // issue 3953: Options::fow.enabled went through; the tiles drawn with the EXE's PS per TerrainFOW style (t6), wrap
    // copies counted as tilesDrawn
    bool fowOn = false;
    uint32_t fowStyleTiles[5] = {0, 0, 0, 0, 0};
    // issue 4305: with a shadow bound, the tiles drawn with the EXE's PS per TerrainShadow style (t7), wrap copies counted
    uint32_t shadowStyleTiles[5] = {0, 0, 0, 0, 0};
    // issue 0371: tiles drawn from a cache by what their t0 is (index = CacheKind: 1 BC1, 2 RGBA8 256 and 3 RGBA8 512 near
    // pairs, 4 other), wrap copies not counted; listKind: the same over the EXE's visible list (when listCounted), drawn
    // by the module's cull or not. The census of 0306 (terrain-bake-passes.md 3.5.1) counts the EXE's frame the same way.
    uint32_t drawnKind[5] = {0, 0, 0, 0, 0};
    uint32_t listKind[5] = {0, 0, 0, 0, 0};
};

// Issue 0371: what a tile's t0 view is: a BC1 cache, a near pair of 256 or 512 (R8G8B8A8, terrain-bake-passes.md 3.5.1),
// something else, or nothing.
enum class CacheKind : uint8_t { None = 0, Bc1 = 1, Rgba256 = 2, Rgba512 = 3, Other = 4 };
CacheKind cacheKindOf(ID3D11ShaderResourceView* v);

// Per-tile colour caches of the EXE (the bake tap, issue 3204): t0 and t8 of the TerrainShader PS.
struct TileCache {
    ID3D11ShaderResourceView* color = nullptr;   // t0 (tile+0x198 / +0x190)
    ID3D11ShaderResourceView* second = nullptr;  // t8 (tile+0x19c / +0x194); read by pass 0 only; non-null selects pass 0
                                                 // (Options::pass -1; the EXE's tile+0x2FC, issue 0189)
    uint32_t generation = 0;
};

struct TileGrid {
    uint32_t tilesX = 0, tilesY = 0;          // tiles covering the grid
    uint32_t tilePlotsX = 0, tilePlotsY = 0;  // plots per tile (P16)
    uint32_t tileSamples = 0;                 // height samples (= patches) per tile edge: tilePlots * 64
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the EXE's terrain programs from the .shader section of exePath (read-only) and the module's own shaders,
    // constant buffers, samplers and constant textures. gameDir: the install root (read-only): the fog ramp
    // `fogramp.dds` as t2 (issue 0188) and the spec map `specscreenmap.dds` as t9 (issue 0189); empty: t2 black, no
    // visible distance fog, t9 black and FlagB 0, no spec (the picture before 0188 and 0189). Every other input is a
    // constant texture. Errors: BadLayout (dev null), Io (EXE missing), BadMagic / Truncated (not a PE), NotFound (no
    // .shader section, a blob the set names is missing, or gameDir given and its fogramp.dds or specscreenmap.dds not
    // found), the gamefs errors of an install that does not open or a texture that does not parse, Device.
    Status init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir);
    // Issue 0198: the same with the install's files read from an index the caller has opened (read only during the
    // call; the renderer keeps no reference to it). Same results and errors as init(dev, exePath, gameDir) with that
    // index's install, without the gamefs errors of opening one.
    Status init(ID3D11Device* dev, const std::wstring& exePath, const gamefs::GameFiles& files);

    // Issue 0188: the fog ramp bound as t2 (the install's fogramp.dds), null without one. For tests.
    ID3D11ShaderResourceView* fogRamp() const;
    // Issue 0189: the spec map bound as t9 of pass 0 (the install's specscreenmap.dds), null without one. For tests.
    ID3D11ShaderResourceView* specMap() const;
    // Issue 0190: t7 while a shadow is bound: the install's `TerrainShadow_<Style>_ColorKey.tga` as the EXE's 32^3 volume
    // (0xD90E74 + 4 * style; c5r::bake::buildColorKeyLut), the identity volume without gameDir or without the file (as
    // the EXE's ColorKeySystem). Issue 4305: one volume per style, bound by the tile's style (setTileStyles), as t6; the
    // five files are byte-equal on every install measured (terrain-surface.md 12.2), so the picture is the same today.
    // shadowLut: the view bound for a style (identity when none; null before init or for style >= 5).
    ID3D11ShaderResourceView* shadowLut(uint32_t style = 0) const;
    bool shadowLutFromFile() const;           // Generic's (style 0) came from a file
    uint32_t shadowLutsFromFile() const;      // bit s: style s's volume came from a file (init or setShadowLut)
    // Replaces style's TerrainShadow volume from the bytes of a ColorKey TGA (null or 0: identity). Errors as setFowLut:
    // NotFound (init not done), BadLayout (style >= 5), buildColorKeyLut's, Device; on error the volume is unchanged.
    Status setShadowLut(uint32_t style, const uint8_t* tga, size_t size);

    // Issue 3953: the TerrainFOW LUTs (t6 with Options::fow on), one 32^3 R8G8B8A8_UNORM volume per style (0 Generic,
    // 1 America, 2 Asia, 3 Africa, 4 Europe; the EXE's table 0xD90E60 + 4 * style). init with a gameDir reads
    // `TerrainFOW_<Style>_ColorKey.tga` through c5r::bake::buildColorKeyLut; a style whose file is missing keeps the
    // identity volume (as the EXE's loader 0x681A00); a file that is there but does not parse fails init (named).
    // fowLut: the view bound for a style (the identity volume when none; null before init or for style >= 5).
    ID3D11ShaderResourceView* fowLut(uint32_t style) const;
    // Bit s set: style s's volume came from a file (init or setFowLut).
    uint32_t fowLutsFromFile() const;
    // Replaces style's volume from the bytes of a ColorKey TGA (tga null or size 0: back to identity). Errors: NotFound
    // (init not done), BadLayout (style >= 5), buildColorKeyLut's (Truncated, Unsupported, BadLayout), Device; on error
    // the volume is unchanged.
    Status setFowLut(uint32_t style, const uint8_t* tga, size_t size);

    // Issue 3953: the tile style (0..4) of one tile of tileGrid(): t6's (TerrainFOW) and, since 4305, t7's
    // (TerrainShadow) volume; 0 (Generic) for every tile after setHeightField. Errors: NotFound (no field), BadLayout (tile outside tileGrid() or style >= 5).
    Status setTileStyle(uint32_t tileX, uint32_t tileY, uint32_t style);
    // Every tile's style by c5r::bake::tileStyle (the EXE's plot-list rule, four plots plus the fifth plot of 0383, no
    // x wrap, over the DLL's continent art types: all 2,240 Tiny and 10,752 huge tiles of 3406's probes,
    // terrain-bake-passes.md 5.3; wrapX is passed on and ignored) from artTypes[y * mapW + x] (the DLL's map: mapW x mapH
    // plots, the field's mapH - 4 rows). A tile of more than one plot takes the style of its first EXE tile (tx * plots,
    // ty * plots) [I: exact only for 1-plot tiles, the EXE's]. Errors: NotFound (no field), BadLayout (artTypes null,
    // mapW or mapH 0, mapW != the field's plots or mapH + 4 != the field's rows).
    Status setTileStyles(const uint8_t* artTypes, uint32_t mapW, uint32_t mapH, bool wrapX);
    // The tile's style (0 for a tile outside the grid or before a field).
    uint32_t tileStyleAt(uint32_t tileX, uint32_t tileY) const;

    // Builds the height textures of g (replaces the previous field and drops every tile cache): since issue 0191 the
    // EXE's 1024 x 1024 A8 pages with 11 mips (terrain-bake-passes.md 5.0) that its tiles use; a tile of 3 plots that
    // does not fit its EXE page gets a page of the same rule whose window starts 64 samples before the tile. Errors:
    // NotFound (init not done), BadLayout (sizes inconsistent: h.size() != samplesX*samplesY, samples != plots*64, mat
    // neither empty nor samplesX*samplesY, sx/sy/sz not finite or <= 0), Unsupported (tilePlots 0 or above
    // kMaxTilePlots, or the plot counts not a multiple of tilePlots), Device.
    Status setHeightField(const heightfield::HeightGrid& g, uint32_t tilePlots = kDefaultTilePlots);

    // Epic 0465, ticket 4909 (live-terrain.md 8.14): the same field with its samples changed inside rects only (a live
    // terrain window). Leaves the renderer in the state setHeightField(g, the current tile plots) would leave it in,
    // except that the tile caches are kept (setHeightField drops them; the residency sets them again) and the tile
    // styles are kept (setHeightField sets Generic; the caller sets them again as after setHeightField): the pages whose
    // 1024-sample window (both axes wrap) meets a rect are rebuilt (all 11 levels) and re-made, the z bounds of the tiles
    // on them recomputed, the material texture and land class of the tiles whose S x S / P + 8 window meets a rect, the
    // EXE list (exeListed) of the 64-sample tiles whose window meets a rect, zmaxAll, grayMark 0, the LOD state reset (as
    // setHeightField's). built: pages already built from g (the live-terrain worker, pagesForRects order or any order);
    // a page not in it is built here. Errors: NotFound (no field), BadLayout (g is not the current field's layout: plots,
    // samples, scales, offsets, wrap, material presence; a rect outside the samples; a built page of another index or
    // size), the builders' and Device. On an error the renderer is left without a field (haveField false) when pages
    // were already replaced, else unchanged; the caller re-applies the whole field (D118: never a half-updated field).
    Status updateHeightRegion(const heightfield::HeightGrid& g, const std::vector<SampleRect>& rects,
                              const std::vector<bake::HeightPage>* built = nullptr, RegionUpdateStats* stats = nullptr);

    // Ticket 4909 (tests and the offline bench): one 64-bit digest per part of the field state (pages: every level
    // read back from the GPU; tiles: page, z bounds, land, style, the material texture read back; exeListed; zmaxAll;
    // the LOD state). Equal digests after setHeightField and after updateHeightRegion of the same g are the bit-equality
    // check of 8.14. Errors: NotFound (no field), Device.
    Status fieldDigest(std::vector<uint64_t>& out, bool withCaches = false) const;

    // Tile grid of the current field (zeros before setHeightField).
    TileGrid tileGrid() const;

    // Sets or replaces the caches of one tile (both views null clears it; a view is AddRef'd and released when
    // replaced, cleared or the field changes). Errors: NotFound (no field set yet), BadLayout (tile outside
    // tileGrid()).
    Status setTileCache(uint32_t tileX, uint32_t tileY, const TileCache& c);

    // Draws the terrain into rtv (and dsv when not null: depth test LESS_EQUAL, write on; cleared to 1 when
    // o.clearDepth; when dsv is null the module uses its own depth buffer of the camera's size). The context's state
    // is changed and not restored. Errors: NotFound (init not done, or no field set), BadLayout (ctx or rtv null,
    // camera width or height 0 or not the target's size, a matrix not finite, pass outside 0..2, tessFactor not
    // finite or outside [0, 64], lodLevel neither kLodExe nor 0..kMaxLodLevel, lodTriSize / lodSubdiv outside 1..64,
    // tileD4 above 1024, WorldPos into a target that is not R32G32B32A32_FLOAT, an overlay matrix not finite, an overlay
    // view that is not a Texture2D R8G8B8A8_UNORM view, a shadow matrix not finite, a shadow strength not finite or outside
    // [0, 1], a shadow view that is not a Texture2D R16_UNORM or R16G16B16A16_FLOAT view), Device. With kLodExe the LOD state advances by one frame
    // (0x4241E0) before the draw; a failed draw leaves it as it was.
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const Camera& cam,
                const Options& o, FrameStats* stats);

    // Ticket 4551 (epic 0431; research/05-graphics-composition/trees.md 11.1, 11.2): the EXE's Screen Overlay [0xDA12A8],
    // the tree shader's t3. Called after the frame's draw() with the same camera and Options (the overlay pair of
    // Options::overlay, the LOD state that draw() left): clears rtv to (0, 0, 0, 0) and draws the tiles draw() draws
    // (same cull, order and wrap copies, each copy with its OverlayProj) with the EXE's pass-2 programs (HS 0x067f9cb8:
    // TessFactorOne; PS 0x067ea680: t1 at the overlay uv) for every tile, cached or not; TerrainTile as draw()'s except
    // the patch list of level L + 1 with its LodStep (twice draw()'s) at draw()'s LodMip, and InvGridSize 0; no depth, no
    // blend, no culling (a later tile overwrites an earlier one), no water plane. The LOD state does not advance; at L =
    // kMaxLodLevel the level-4 list is drawn with its own LodStep [I: stock has no level 5]. rtv: the camera's size,
    // R8G8B8A8_UNORM (stock's has 12 mips and GenerateMips after the pass; the tree PS samples mip 0 only, trees.md 11.4:
    // the caller may keep one mip). Errors: as draw() (the dsv and depth options are not used), plus BadLayout for a
    // target that is not R8G8B8A8_UNORM. stats: tilesDrawn, tilesCulled, passUsed 2, the LOD fields of the draw.
    Status drawScreenOverlay(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, const Camera& cam, const Options& o,
                             FrameStats* stats);

    // Issue 0191: the EXE-rule LOD state (TR+0x2424..+0x2430) the next kLodExe draw starts from; zero (TessFactor =
    // Subdiv 2) after init and setHeightField, as the EXE's at terrain creation. resetLod sets it back to that.
    LodState lodState() const;
    void resetLod();

    // GPU time of the newest draw whose timestamps are available (ms); with wait, blocks (flushes) until the last
    // draw's are. Errors: NotFound (no draw yet, or not available without wait), Device (disjoint timer).
    Status gpuTime(ID3D11DeviceContext* ctx, bool wait, double* ms);
    // Ticket 0753 (J3): the draw's timestamps around the execute of a command list that holds a draw made with
    // Options::gpuTiming false (on the immediate context: the same query ring, poll and order as draw()'s own); gpuTime
    // and FrameStats::gpuMs then read the list's time. A begin without its end is dropped at the next begin.
    void gpuTimingBegin(ID3D11DeviceContext* ctx);
    void gpuTimingEnd(ID3D11DeviceContext* ctx);

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
    // Both inits: files, else (openDir not empty) an index of openDir opened where the original init opened it (after
    // the EXE's shaders, so the errors come in the same order), else no install.
    Status initFrom(ID3D11Device* dev, const std::wstring& exePath, const gamefs::GameFiles* files, const std::wstring& openDir);
    // draw() (screenOverlay false) and drawScreenOverlay() (true, ticket 4551) share this body.
    Status drawPass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const Camera& cam,
                    const Options& o, FrameStats* stats, bool screenOverlay);
};

}} // namespace c5r::terrain
