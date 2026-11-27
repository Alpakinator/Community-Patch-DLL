// c5r/water/water.h - the stock game's water drawn as the EXE draws it (epic 3600, seam C of package I2-E).
//
// First version committed by the team-3600 lead as the contract between the water module (issue 3604, owner of this
// file from then on; rivers 3605 and waves 3606 add to it through 3604's owner by the rule below) and its users:
// render/tools/watershot (offline, from a RenderDoc capture's own constants, buffers and textures) and server.exe's
// water scene (issue 3607, server/src/render/srv_water_scene.cpp). Facts it rests on:
// research/05-graphics-composition/water-system.md (what draws water, the meshes, the open questions; section 10 is this
// module: the WaterFrame writer, the ocean geometry and the inputs, as behaviour, D83), asset-pipeline.md 3.1-3.4
// (WaterShader slots t0-t13, WaterFrame 0x250 B, samplers), terrain-bake-passes.md 5.0 (the 8-bit height pages).
//
// What the module does: with the EXE's own WaterShader (91) programs, read from the player's CivilizationV_DX11.exe at
// run time through c5r::shaders / c5r::terrainsets (D83), it fills WaterFrame from the camera, the Water object's fields
// and the world's globals, builds the ocean geometry the EXE builds every frame (one quad per 64 x 64-sample block of the
// view's ground footprint, grouped by height page; water-system.md 10.2), and draws ocean / coast / lakes with the
// WaterShader pass the EXE uses at the quality setting (Water+0xE8: 0, 2 or 3; the owner's TerrainWaterQuality 2 -> pass 3
// [I until 3601]). Rivers (3605) and coast waves (3606) take their meshes from the EXE through the water tap
// (setMesh); the ocean has no mesh to tap (P4 failed statically: it is built per frame) and is generated here.
// Inputs owned by other seams are constant textures wired as parameters (setTexture), so that a later seam replaces
// them without editing this module: FOWMask t10 "visible" and TransFOWColor t11 identity (seam F), OverlayMap t9
// transparent (seam E), shadow t13 white, reflection t4 the EXE's own "no reflection" input (a zero texture, decision 3
// (ii)), the currents map t5 neutral and the LEAN bump pair t0/t1 flat until issue 3602's loaders land.
//
// Conventions: as c5r/terrain/terrain.h (D3DX row-major matrices, row vectors, the EXE's world units and axes).
// Errors: every call returns a c5r::Status; nothing throws; bad input is a named Err, never a crash. A missing tap is
// not an error of draw(): systems without a mesh are skipped and counted in FrameStats (the scene logs it).
//
// Changes against the lead's first version (2026-11-14, issue 3604, W4), each from the static reading of the writer
// 0x739ae0 and Water::Startup 0x5F06A0 (water-system.md 10):
//  - FrameInputs: fogParams[4] replaced by Fog (the four DistanceFog globals the writer reads); new World (the world box
//    and wrap period 0xD90FCC..0xD90FE8, derived from the height field when not given), overlayProj, shadowMatrix0/1,
//    shadowBlend (0xD90FA8), camPos (0xDA1440 + 0x40v, computed from the view when not given); Clock is applied as
//    Water+0xDC (the only time-like member of WaterFrame [I]); waterZ now overrides Water+0xC8 (WaveR12 is the water
//    height the VS draws at).
//  - WaterFields: named accessors (get/set by Water offset) and stockWaterFields(), the values Water::Startup leaves
//    (including the six parameter texels of watersky.dds, waterFieldsFromSky()).
//  - New free functions: worldFromGrid, viewFootprint, buildOceanGeometry (the per-frame ocean mesh, exposed for tests
//    and watershot).
//  - Renderer: new setTexture (replace or restore a constant input), oceanGeometry (the last frame's mesh); setMesh
//    rejects System::Ocean with Unsupported (the ocean is generated); Options::oceanPass -1 = 3; Options gains
//    oceanBlend / oceanDepthWrite (the pass states are not recovered yet: 3601 question 6) and wrapCopies;
//    FrameStats gains oceanQuads, pagesMissing, indexOverflow.
// Changes for the rivers (2026-11-14, issue 3605, W4; water-system.md 11):
//  - New RiverSlot, RiverArt / loadRiverArt (the river's art from the install, CPU side), riverColorKeyFile, and
//    RiverRenderer (render/src/water/river.cpp): WaterShader pass 4 (or 1) over the tapped river mesh with the river's
//    own buffer (identity World), the frame's WaterFrame, its art and the shared inputs of the ocean.
//  - Renderer: init also creates the river part; setMesh(System::River, m) uploads the mesh to it; draw() draws the
//    rivers after the ocean when Options::rivers and a river mesh is set (FrameStats::riverDraws, riverPassUsed);
//    new setRiverTexture. Options::riverPass: -1 = 4 (the owner's TerrainWaterQuality 2 [L]), else 1 or 4; riverStyle
//    selects the Rivers ColorKey (0 Generic .. 4 Europe; the EXE binds Generic [C]).
// Changes for the overlay (2026-11-14, issue 3704, epic 0167, 0167-W4; overlay-system.md 4-5, 10): FrameInputs gains
// haveOverlayProjWrap / overlayProjWrap, the OverlayProj of the EXE's map-wrap views for draw()'s views 1 and 2.
// Changes for the shadow (2026-11-15, issue 4308, epic 0169; terrain-surface.md 11.10): FrameInputs gains
// haveShadowMatrix0Wrap / shadowMatrix0Wrap (the wrap views' ShadowMatrix0, as the overlay's); t13 is set by the scene
// through setTexture(Slot::Shadow) (the EXE's ShadowBlur copy: R16_UNORM, or R16G16B16A16_FLOAT at quality 1; the PS reads
// .x) and shadowBlend is the same frame's strength.
// Changes for the live look (2026-11-14, issue 3609, W4; water-system.md 10.7): the module's own ocean inputs are now
// stock's: t0 / t1 the waterbumps16 LEAN pair from the install (the flat constant made the PS's LEAN variance 0: white
// and black sea), t5 3602's currents map (computed at the first draw after setHeightField), t13 zero (no shadow; was
// white), t11 the TerrainFOW Generic ColorKey when the file exists; Fog defaults to the stock DistanceFog values with
// w (0xD90FAC) from the camera height (zoomFraction); FrameInputs::shadowBlend defaults to 1 (stock). New
// Renderer::inputTexture and zoomFraction.
// Changes for the reflected ice (2026-11-16, issue 0382; water-system.md 13.6): the reflection camera (ReflectionCamera,
// reflectionCamera, reflectionDirection, kReflectionSize), and draw() gives the wrap views T(shift) x shadowMatrix1 (the
// EXE's 0xDA1890 + 0x40v) instead of view 0's matrix.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"

namespace c5r { namespace water {

using Camera = terrain::Camera;

const uint32_t kWaterFrameBytes = 0x250;   // WaterFrame, shared id 26 (asset-pipeline.md 3.3) [C]
const uint32_t kCurrentsSize = 256;        // the "Water Currents" target, 256 x 256 (asset-pipeline.md 3.4) [C]
const uint32_t kOceanBlock = 64;           // samples per ocean quad side (one plot) [C, 0x739ae0]
const uint32_t kPageStep = 896;            // samples per height page step; pages 16 x 16 at most [C]
const uint32_t kMaxViews = 4;              // the writer's per-view counters (0x800 bytes = 4 x 256 words) [C]

// The Water object's fields that WaterFrame takes (Water = 0xD3DD0C). Raw bytes Water+0x60 .. Water+0xE8.
struct WaterFields {
    uint8_t raw[0x88] = {0};   // Water+0x60 .. Water+0xE8
    bool valid = false;
    // The float at Water+off (0x60 <= off <= 0xE4, 4-aligned); 0 outside.
    float get(uint32_t off) const;
    void set(uint32_t off, float v);
};

// What Water::Startup and its texture load leave in the fields [C code, water-system.md 10.3]: +0x60/+0x64 0.1, +0x68 2,
// +0x6C 0.5, +0x7C 2^11.75, +0x8C 2^5.75, +0xC8 level * (1/65535) (TerrainSystem passes 0x32C8 = 13000), +0xCC 1.43,
// +0xD0 5, +0xD4 0.008 (Water_SetScale), +0xD8 0.18 (Water_SetSpeed), +0xDC 0, +0xE0 0.03, +0xE4 0, and +0x70..+0xC4
// from the first six texels of watersky.dds (waterFieldsFromSky). sky: 6 texels RGBA as floats 0..1 (row-major from
// the top-left texel), or null for zeros there.
WaterFields stockWaterFields(const float (*sky)[4], uint16_t level = 13000);
// +0x70..+0xC4 from six float RGBA texels: +0x70 = t0.rgb / t0.a, +0x80 = t1.rgb / t1.a, +0x90 = normalize(t2.rgb -
// 0.5), +0x9C = normalize(t3.rgb - 0.5), +0xA8 = normalize(t4.rgb - 0.5) with +0xB4 = t4.a, +0xB8 = normalize(t5.rgb -
// 0.5) with +0xC4 = t5.a. +0x7C and +0x8C are kept.
void waterFieldsFromSky(const float sky[6][4], WaterFields& f);

// The water clock (decision 4): (i) the EXE's water time from the frame record once vp_frame.h carries it; (ii) the
// server's own clock at the EXE's speed until then. Applied as Water+0xDC (WaveR11 = Water+0xDC * WavePhase.x, the VS's
// texture-coordinate offset) when `apply`; the writer reads no other time [C], so +0xDC is the water time [I].
struct Clock {
    double seconds = 0.0;      // the EXE's water time (unit as 3601 finds it)
    bool fromExe = false;      // true: (i); false: (ii), in phase only by chance
    bool apply = false;        // false: Water+0xDC as in `fields`
};

// The world globals the writer reads [C]: 0xD90FCC/D0 (world box min x, y), 0xD90FD8 (max x), 0xD90FE4 (wrap period =
// world width), 0xD90FE8 (world height), GridHeightmap+0xC/+0x10 (samples).
struct World {
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    float width = 0, height = 0;
    float sx = 1.0f, sy = 0.866f, sz = 0.2f;  // HexScale 0xB9A6D4..DC (the file's values; stock 1, 0.866, 0.2)
    uint32_t samplesX = 0, samplesY = 0;
    bool wrapX = false;
    bool valid = false;
};
// From a height field: minX = -(samplesX - 1) * sx / 2, maxX = -minX, width = (samplesX - 1) * sx, height =
// (samplesY - 1) * sy, minY = -height / 2: bit-equal to the EXE's globals on the Tiny and Huge ground truth [L, epic
// 3000's grid_constants.json; test world_from_grid_equals_the_exe_globals].
World worldFromGrid(const heightfield::HeightGrid& g);

// The DistanceFog globals the writer reads (0xDA1C50..0xDA1C6C; seam F leaves them at the EXE's values, 3601).
// Defaults: the stock values [L, both of 3601's captures, water-system.md 10.7] (issue 3609; they were "no fog": density
// 0, colour grey, which made the ocean PS's fog term 0 / 0).
struct Fog {
    // issue 4305: the EXE's rule n * (1 / 255) in float32 (0x3F129293, 0x3F36B6B7, 0x3F73F3F5); n / 255 gave a blue
    // 1 ulp low (0x3F73F3F4), terrain-surface.md 12.4
    float color[4] = {146.0f * (1.0f / 255.0f), 182.0f * (1.0f / 255.0f), 243.0f * (1.0f / 255.0f), 1.0f};  // 0xDA1C50..5C, 1
    float density = 0.05f;                      // 0xDA1C60 (0: no fog; FogParams.y is then 0, not -inf)
    float scale = 5.0e-5f;                      // 0xDA1C64
    float w = -1.0f;                            // 0xDA1C6C = 0xD90FAC: the camera's zoom fraction; < 0: zoomFraction() of
                                                // the camera position's height (computeWaterFrame)
};

// 0xD90FAC as the camera controller writes it (0x742920): (zoom - min) / (max - min), zoom = ln(height / 50) * 6.6667
// (camera-and-picking.md, GVS+0x140), min 11 and max 20.5 (config.ini defaults). The controller takes its target's
// height; the camera's height is within a few thousandths of a level of it at rest [L 3601's cameras: 16.9997 for 17,
// 11.017 for 11 (clamped)], so this is [I] to that precision. Clamped to [0, 1].
float zoomFraction(float cameraHeight, float zoomMin = 11.0f, float zoomMax = 20.5f);

struct FrameInputs {
    Camera cam;
    WaterFields fields;
    Clock clock;
    float waterZ = -1.0f;      // >= 0: overrides Water+0xC8 as waterZ / (0.2 * 255); < 0: the field as given
    Fog fog;
    World world;               // !valid: from the height field (draw); computeWaterFrame needs it valid
    float overlayProj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};    // 0xDA1690 + 0x40v
    // Issue 3704 (epic 0167): the OverlayProj of the EXE views that show the map-wrap copies, [0] for the view at -width,
    // [1] for the view at +width (draw()'s views 1 and 2), as the EXE stores them (0xDA1690 + 0x40v; the geometry stays
    // in world coordinates and the view moves, as in the EXE, so the matrix is used unchanged). Used only when
    // haveOverlayProjWrap; otherwise every view takes overlayProj (the behaviour before 3704).
    bool haveOverlayProjWrap = false;
    float overlayProjWrap[2][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    float shadowMatrix0[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // 0xDA1790 + 0x40v
    // Issue 4308 (epic 0169): the ShadowMatrix0 of the EXE views that show the map-wrap copies, [0] for the view at -width,
    // [1] for the view at +width (draw()'s views 1 and 2), as the EXE stores them (0xDA1790 + 0x40v), used unchanged as the
    // overlay's wrap matrices are. Used only when haveShadowMatrix0Wrap; otherwise every view takes shadowMatrix0.
    bool haveShadowMatrix0Wrap = false;
    float shadowMatrix0Wrap[2][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    // 0xDA1890 (view 0): the reflection's view-projection (ReflectionCamera::viewProj, issue 0382); draw() gives the wrap
    // views T(shift) x this, as the EXE's 0x615950 does. Identity with the empty t4 (decision 3 (ii)): no effect there.
    float shadowMatrix1[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float shadowBlend = 1.0f;  // 0xD90FA8 (GVS+0x78C, the camera's eased shadow factor; 1.0 in both captures [L])
    bool haveCamPos = false;   // false: camera position = inverse(view) row 3 (the EXE's 0xDA1440 + 0x40v)
    float camPos[3] = {0, 0, 0};
};

// WaterFrame for one view, byte for byte as the EXE's writer 0x739ae0 fills it (water-system.md 10.1). Bytes the writer
// does not write are 0. Float tolerance (acceptance A3): every member bit-equal except FogParams.y (x87 exp, 1 ulp),
// CameraPos (a matrix inverse, 2 ulp relative) and ViewProj (summation order, 2 ulp relative) when computed from the
// view; given camPos and an exact ViewProj they are bit-equal too. Errors: BadLayout (out null, a matrix, the clock,
// waterZ or a world value not finite, world not valid or width / samplesX 0), NotFound (fields not valid).
Status computeWaterFrame(const FrameInputs& in, uint8_t out[kWaterFrameBytes]);

// The view's ground footprint the EXE keeps per view at 0xDA1B00 (min x, y) and 0xDA1B30 (max x, y): the four edge lines
// of the frustum (pairs of side planes) cut with the planes z = 0 and z = 127.5, bounding box of the 8 points, clamped to
// the world box [C 0x5D0370]. Errors: BadLayout (matrix not finite, world not valid).
struct Footprint {
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
};
Status viewFootprint(const float viewProj[16], const World& w, Footprint& out);

// The ocean mesh of one frame (0x739ae0), for views 0..n-1: per view, per 64-sample block (bx, by) of its footprint
// (sample coordinates (x - minX) / sx and (y - minY) / sy rounded half away from zero, blocks [s0/64, s1/64 + 1),
// clamped to [0, samples - 1]), one quad of 4 vertices (x, y, u, v): x = bx*64*sx + 32*sx + minX -+ 32*sx, y likewise
// with sy, u = ((x - minX) * samplesX / width + 64 - px*896) / 1024, v = -((y - minY) * samplesY / height + 64 -
// py*896) / 1024 for its page (px, py) = (bx*64/896, by*64/896); vertex order (left, top), (right, top), (right,
// bottom), (left, bottom);
// indices 4q+{0,1,2,0,2,3} (16-bit, as the EXE: wraps above 65535 vertices, counted). Quads are stored grouped by
// (view, page py*16 + px) in that order; batches list each non-empty group (one draw each).
struct OceanBatch {
    uint32_t view = 0, page = 0, firstQuad = 0, quads = 0;
};
struct OceanGeometry {
    std::vector<float> vertices;     // 16 floats per quad
    std::vector<uint16_t> indices;   // 6 per quad
    std::vector<OceanBatch> batches;
    uint32_t quads = 0;
    bool indexOverflow = false;      // a quad's vertex index passed 65535 (the EXE's 16-bit indices wrap)
};
// Errors: BadLayout (world not valid, views 0 or above kMaxViews, a footprint not finite), Unsupported (a page index
// outside 16 x 16).
Status buildOceanGeometry(const World& w, const Footprint* views, uint32_t viewCount, OceanGeometry& out);

// ---- The reflection camera (issue 0382; water-system.md 13.6) ----
// "Reflection Texture Raw" (t4) is not a mirror image: the EXE draws the models and the polar ice into it with an
// orthographic camera that looks along a fixed direction over view 0's footprint, and the ocean and river PSs read it at
// the world position projected by WaterFrame 0x160 (0xDA1890, which the old notes called "ShadowMatrix1"). The rule is the
// second half of the shadow set-up 0x615950 [C]:
//   rectangle = view 0's unclamped footprint (0xDA1B60 min, 0xDA1B90 max: the 8 points of viewFootprint before its clamp
//               to the world box, z 0 at both ends) [C 0x5D0370];
//   center    = ((min + max) * 0.5, z 0); l = (min.x - max.x) * 0.5, b = (min.y - max.y) * 0.5 (SSE);
//   eye       = center - 5000 * D, D = TerrainSystem+0x75744 = (-cos 80 deg * sin 135 deg, -sin 80 deg * sin 135 deg,
//               cos 135 deg) (x87 from the EXE's double radians, stored as float);
//   view      = D3DXMatrixLookAtRH(eye, center, (0, 1, 0))                           -> 0xDA1990
//   proj      = D3DXMatrixOrthoOffCenterRH(l, -l, b, -b, 30000, -30000)                -> 0xDA1A90
//   viewProj  = view x proj (D3DXMatrixMultiply)                                       -> 0xDA1890 = WaterFrame 0x160
// For the EXE's map-wrap views v >= 1: 0xDA1890 + 0x40v = (g_View[v] x inverse(g_View[0])) x viewProj = T(shift_v) x
// viewProj (Renderer::draw applies it to its views 1 and 2), and the reflection pass draws only once, with view 0's
// camera. Checked [L, 2026-11-16] against four stock captures (4301's Tiny loads 1 and 2: the ice reflection draws' rows
// 0-3 and camera row 12; 3601's cam1 and cam2: WaterFrame 0x160): relative difference below 1.3e-6 in every element.
struct ReflectionCamera {
    float view[16] = {};
    float proj[16] = {};
    float viewProj[16] = {};
    float center[3] = {0, 0, 0};
    float eye[3] = {0, 0, 0};
    float halfX = 0, halfY = 0;   // -l, -b
};
const uint32_t kReflectionSize = 1024;   // "Reflection Texture Raw" when the ocean pass is not 0 [C 0x5F06A0, L 3601]
// D above, as the EXE stores it.
void reflectionDirection(float out[3]);
// From the scene's view 0 view-projection (g_View[0] x g_Proj). Errors: BadLayout (matrix not finite, a footprint edge
// parallel to the ground (no point), a rectangle of zero width or height, a result not finite).
Status reflectionCamera(const float sceneViewProj[16], ReflectionCamera& out);

// One tapped mesh (CPU bytes from the water tap image, or from a capture for watershot).
struct Mesh {
    std::vector<uint8_t> vb, ib;
    uint32_t stride = 0;
    uint32_t indexFormat = 0;  // DXGI_FORMAT_R16_UINT (57) or R32_UINT (42)
    struct Draw {
        uint32_t topology = 0, vertexCount = 0, indexCount = 0, startIndex = 0, vbOffset = 0, pass = 0;
        int32_t baseVertex = 0;
    };
    std::vector<Draw> draws;
    std::vector<uint8_t> ownConstants;   // e.g. the river own buffer (0x80, "river World") when static
};

enum class System : uint32_t { River = 1, Ocean = 2, Wave = 3 };   // = VPShellWaterSystem

// WaterShader texture slots the module binds (asset-pipeline.md 3.4).
enum class Slot : uint32_t {
    Lean0 = 0, Lean1 = 1, Reflection = 4, Currents = 5, WaterColor = 6, FogRamp = 8, Overlay = 9, FowMask = 10,
    TransFow = 11, Shadow = 13
};

// River texture slots (the river draw record River+0x18, water-system.md 9.2 / 11).
enum class RiverSlot : uint32_t { Lean0 = 0, Lean1 = 1, Lean2 = 2, Lean3 = 3, Alpha = 5, Diffuse = 6, Height = 7, ColorKey = 12 };

// The river's art as the EXE loads it, CPU side (for the renderer and for watershot's input comparison): the two
// .lean files (riversmooth16 -> t0/t1, riverrough_new16 -> t2/t3), river_alpha_new.dds (t5), river_diffuse_new.dds
// (t6, sampled through its sRGB view) and the five Rivers ColorKey volumes (32^3 RGBA8, the terrain bake's TGA rule;
// a missing file leaves the identity volume, as the EXE's ColorKeySystem does).
struct RiverArt {
    std::vector<uint8_t> leanBytes[2];
    std::vector<uint8_t> alphaDds, diffuseDds;
    std::vector<uint8_t> colorKey[5];   // 32 * 32 * 32 * 4 bytes each
    bool colorKeyFromFile[5] = {false, false, false, false, false};
};
std::string riverColorKeyFile(uint32_t style);   // "Rivers_<Style>_ColorKey.tga"; "" for style >= 5
// Errors: NotFound (a .lean or .dds missing, named), the parsers' errors (Truncated / BadMagic / BadLayout /
// Unsupported).
Status loadRiverArt(const std::wstring& gameDir, RiverArt& out);

// Inputs the river shares with the ocean draw: t4 (reflection), t8 (fog ramp), t9 (overlay), t10 (FOW mask), t11
// (TransFOW LUT) by slot, and the EXE's 13 sampler states.
struct RiverShared {
    ID3D11ShaderResourceView* srv[14] = {};
    ID3D11SamplerState* samplers[13] = {};
};

class RiverRenderer {
public:
    RiverRenderer();
    ~RiverRenderer();
    RiverRenderer(const RiverRenderer&) = delete;
    RiverRenderer& operator=(const RiverRenderer&) = delete;
    // The river VS 0x06816A20 and PSs of passes 1 and 4 from the EXE, the own buffer, the states of the stock river
    // draw, and the art (gameDir empty: constant art: flat LEAN, white alpha, mid-blue diffuse, identity ColorKey).
    // Errors: BadLayout (dev null), the EXE's errors, NotFound (a blob or art file missing), Device.
    Status init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir);
    // The tapped river mesh (an empty Mesh clears it): stride 32 (the EXE's vertex format 0xA: POSITION float3,
    // TEXCOORD0 float2, TEXCOORD1 float2, COLOR, the last not read by the VS), R32 or R16 indices, triangle-list draws.
    // Errors: NotFound (init not done), BadLayout (stride not 32, index size, a draw outside its buffers, topology not a
    // triangle list, pass not 0, 1 or 4, own constants not 0 or >= 64 bytes), Device.
    Status setMesh(const Mesh& m);
    bool hasMesh() const;
    // Replaces a river input, or restores the module's own when srv is null. Errors: NotFound (init), BadLayout (slot).
    Status setTexture(RiverSlot slot, ID3D11ShaderResourceView* srv);
    // Draws every mesh draw with WaterFrame `frame` into rtv / dsv (depth test LESS_EQUAL with write, as stock). pass -1:
    // the draw's own pass (the tap's), else 4. Errors: NotFound (init, no mesh), BadLayout (ctx, rtv or frame null,
    // width / height 0, pass not -1, 1 or 4, style >= 5), Device.
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t width,
                uint32_t height, const uint8_t frame[kWaterFrameBytes], const RiverShared& shared, int pass, uint32_t style,
                uint32_t* draws, int* passUsed);
    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

struct Options {
    bool ocean = true, rivers = true, waves = true;
    int oceanPass = -1;        // -1 = 3 (Water+0xE8 at TerrainWaterQuality 2 [I]); 0, 2 or 3 (5 = the EXE's no-texture pass)
    int riverPass = -1;        // -1 = the tapped draw's pass (4 at the owner's settings [L]); else 1 or 4
    bool computeCurrents = true;  // recompute the currents map when the height field changed (3602)
    bool clearDepth = false;      // the scene draws water after the terrain into the terrain's depth
    uint32_t riverStyle = 0;      // Rivers ColorKey style (0..4)
    // Pass states of the ocean as the stock capture shows them [L, water-system.md 10.6]: blend SRC_ALPHA /
    // INV_SRC_ALPHA (alpha ONE / ONE), depth test LESS_EQUAL without write, cull FRONT with FrontCounterClockwise.
    // The two switches below are for diagnostics only.
    bool oceanBlend = true;
    bool oceanDepthWrite = false;
    bool wrapCopies = true;       // when the field wraps: views at -width and +width as the EXE's map-wrap views
};

struct FrameStats {
    uint32_t oceanDraws = 0, riverDraws = 0, waveDraws = 0;
    uint32_t systemsMissing = 0;  // bit (1 << System) for each system enabled in Options but without a mesh
    uint32_t oceanQuads = 0;
    uint32_t pagesMissing = 0;    // ocean batches whose page lies outside the field's pages (drawn with a null t7, as the EXE)
    bool indexOverflow = false;
    double cpuMs = 0;
    double gpuMs = -1;
    int oceanPassUsed = -1, riverPassUsed = -1;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Creates the EXE's water programs from the .shader section of exePath (read-only), loads the water art from
    // gameDir through c5r::gamefs (D83: read at run time, never copied into the repository): waterdepthcolor_new.dds
    // (t6, sRGB view), fogramp.dds (t8), watersky.dds (the field texels, stockFields()), and the module's constant
    // inputs. gameDir empty: constant art (a mid-blue t6, white t8, zero sky texels) and stockFields() without the sky.
    // Errors: BadLayout (dev null), Io / BadMagic / Truncated (EXE), NotFound (a blob or an art file missing, named in
    // msg), Device.
    Status init(ID3D11Device* dev, const std::wstring& exePath, const std::wstring& gameDir);

    // The fields Water::Startup leaves with this install's watersky.dds (stockWaterFields); valid after init.
    WaterFields stockFields() const;

    // The height field: builds the per-cell 8-bit height pages (t7) by the rule of terrain-bake-passes.md 5.0 (the
    // bake's own code, c5r::bake::buildHeightPage, 11 levels) and marks the currents map stale. Errors: NotFound (init
    // not done), BadLayout (as terrain::Renderer::setHeightField).
    Status setHeightField(const heightfield::HeightGrid& g);

    // Epic 0465, ticket 4909 (live-terrain.md 8.14): the same field with its samples changed inside rects only. Rebuilds
    // and re-makes only the pages whose window meets a rect (terrain::pagesForRects), so the module ends in the state
    // setHeightField(g) leaves, except that the currents map is not marked stale (it depends on the plot counts only,
    // currentsParamsFromGrid). built: pages already built from g (any order); a page not in it is built here. Errors:
    // NotFound (no field), BadLayout (another layout: plots, samples, scales, offsets; a rect outside; a built page of
    // another size), the builders' and Device; on an error nothing is replaced.
    Status updateHeightRegion(const heightfield::HeightGrid& g, const std::vector<terrain::SampleRect>& rects,
                              const std::vector<bake::HeightPage>* built = nullptr, terrain::RegionUpdateStats* stats = nullptr);
    // Ticket 4909: [0] every page read back, [1] the world block and the currents' state. Errors: NotFound, Device.
    Status fieldDigest(std::vector<uint64_t>& out) const;

    // Replaces one system's mesh (an empty Mesh clears it). Errors: NotFound (init not done), Unsupported (System::Ocean:
    // generated per frame), BadLayout (stride 0, ib size not a multiple of the index size, a draw outside its buffers,
    // unknown topology or pass), Device.
    Status setMesh(System s, const Mesh& m);

    // Replaces a constant input (srv AddRef'd), or restores the module's own when srv is null. Errors: NotFound (init
    // not done), BadLayout (slot not one of Slot).
    Status setTexture(Slot slot, ID3D11ShaderResourceView* srv);
    // The same for the river's own inputs (RiverRenderer::setTexture).
    Status setRiverTexture(RiverSlot slot, ID3D11ShaderResourceView* srv);

    // Draws the water into rtv (and dsv when not null). The context's state is changed and not restored. Errors:
    // NotFound (init or height field missing, fields not valid), BadLayout (ctx or rtv null, camera size 0 or not the
    // target's, a matrix (overlayProjWrap included) or the clock not finite, oceanPass not -1, 0, 2, 3 or 5), Device.
    // The overlay (issue 3704): t9 through setTexture(Slot::Overlay, srv), the matrices through FrameInputs.
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
                const FrameInputs& in, const Options& o, FrameStats* stats);

    // The view bound at an input slot (setTexture's, else the module's own); nullptr before init or for other slots.
    // For watershot's input comparisons (issue 3609). The currents map (t5) is the one of the newest draw.
    ID3D11ShaderResourceView* inputTexture(Slot slot) const;

    // The ocean mesh and the WaterFrame blocks of the newest draw (for watershot and the tests).
    const OceanGeometry& oceanGeometry() const;
    const std::vector<std::vector<uint8_t>>& frames() const;

    // GPU time of the newest draw whose timestamps are available (ms), as terrain::Renderer::gpuTime.
    Status gpuTime(ID3D11DeviceContext* ctx, bool wait, double* ms);

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::water
