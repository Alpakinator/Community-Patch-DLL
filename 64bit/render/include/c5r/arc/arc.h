// c5r/arc/arc.h - the ranged-attack arc of the server's world renderer (ticket 0199 part 1, increment 3 groundwork, D143).
//
// What stock draws (research/02-exe-internals/arrows-and-movement-path.md section 1 and
// research/05-graphics-composition/arrows-and-path-line.md sections 3 and 8; behaviour only, D83):
//   - one arrow record: Events.SpawnArrowEvent(x0, y0, x1, y1) sets its start (the start plot's centre at the ground
//     height) and its end (the target plot's centre at ArrowSettings.xml's target_height), shows it and restarts its
//     growth; Events.RemoveAllArrowsEvent hides it (Arrow below);
//   - every frame the arc grows from the attacker to the target in anim_speed seconds and its mesh is rebuilt: a clamped
//     quadratic B-spline through four control points, 16-vertex rings every 0.04 of the curve parameter and a 68-vertex
//     head (buildMesh: 468 vertices and 1260 indices for 0179's capture);
//   - the world pass draws it twice with the scene's depth: an alpha-only pre-pass (blend off), then SRC_ALPHA /
//     INV_SRC_ALPHA; depth LESS_EQUAL with write, cull front, front counter-clockwise; ArrowShader with one 96-byte
//     constant buffer (view-projection, alpha, ambient) and no texture (Renderer).
// The programs are read from the player's CivilizationV_DX11.exe and ArrowSettings.xml from the player's install at run
// time (D83); nothing of either is in this repository.
//
// Part 2 of 0199 (after epic 0431) wires this into the server's scene and clears the EXE's Arrows view bit.
#pragma once
#include <c5r/heightfield/heightfield.h>
#include <c5r/status.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace c5r {
namespace arc {

// ArrowSettings.xml (Assets/UI/Overlays/, a loose file): one <Arrow> element whose attributes are read by name. The
// defaults are the values of the workstation install's file (SHA-256 2bcbad95..., arrows-and-movement-path.md 1.5);
// a missing attribute keeps its default here [I: the EXE keeps the object's previous value, 0 at start-up].
struct Settings {
    float minWidth = 3.0f;          // half-width of the body at the start of the curve
    float maxWidth = 5.0f;          // ... at the end
    float width2 = 1.5f;            // half-height of the body
    float outlineWidth = 0.65f;     // added to the width for the outline; the head's collar length
    float outlineWidth2 = 0.65f;    // added to width2 for the outline
    float arcHeight = 75.0f;        // z added to the two inner control points
    float targetHeight = 20.0f;     // z of the end point (an absolute world z)
    float animSpeed = 0.5f;         // growth time in seconds
    float alpha = 0.5f;             // shader constant row 4.x
    float ambient = 0.7f;           // ambient_light_level, shader constant row 5.x
    uint32_t bodyColor = 0xFF0000FFu;     // R8G8B8A8_UNORM as a little-endian dword (0xAABBGGRR): red
    uint32_t outlineColor = 0xFFFFFFFFu;  // white
};

// Parses the file's bytes. Numbers are read as a C stream reads them (".65" is 0.65); colours as "R,G,B" (0-255 each,
// alpha 255). Errors: BadLayout (not XML, no <Arrow> root, a value that is not a number or a colour, a number not
// finite, anim_speed not > 0, a width negative). `note` lists the attributes that were missing (defaults kept).
Status parseSettings(const char* xml, size_t size, Settings& out, std::string* note = nullptr);
// ArrowSettings.xml from the install at gameDir through c5r::gamefs (loose files in the game's order).
// Errors: Io (the install not readable), NotFound (no ArrowSettings.xml), parseSettings' errors.
Status loadSettings(const std::wstring& gameDir, Settings& out, std::string* origin = nullptr);
static const char* const kSettingsFile = "ArrowSettings.xml";

// The vertex (40 bytes; arrows-and-path-line.md 3): POSITION float3 @0, NORMAL float3 @12, COLOR0 R8G8B8A8_UNORM @24,
// COLOR1 R8G8B8A8_UNORM @28, TEXCOORD0 float2 @32 (never written by the EXE's builder nor read by its shader: 0 here).
struct Vertex {
    float pos[3];
    float normal[3];
    uint32_t color0;
    uint32_t color1;
    float uv[2];
};
static_assert(sizeof(Vertex) == 40, "the arc vertex is 40 bytes");

static const uint32_t kRingVertices = 16;
static const uint32_t kRingJoinIndices = 48;
static const uint32_t kHeadVertices = 68;     // collar 16 + plate 26 + outline 26
static const uint32_t kHeadIndices = 108;     // 24 + 42 + 42
static const uint32_t kMaxVertices = 4000;    // stock's dynamic vertex buffer
static const uint32_t kMaxIndices = 3000;     // stock's dynamic index buffer (R32_UINT)
static const float kMaxProgress = 0.9999f;    // growth stops here

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;    // triangle list, 32-bit
    uint32_t rings = 0;
    float sEnd = 0.0f;                // the curve parameter of the last ring and the head
    bool earlyStop = false;           // the body ended because the head reaches the end point
};

// The mesh at growth `progress` (clamped to [0, kMaxProgress]) from start (world x, y, z) to end (world x, y, z),
// arrows-and-movement-path.md 1.6. Errors: BadLayout (a value not finite, progress not finite or negative).
// A start equal to the end gives a degenerate but finite mesh (zero normals), as the EXE's normalise does [I].
Status buildMesh(const Settings& s, const float start[3], const float end[3], float progress, Mesh& out);

// The one arrow record (arrows-and-movement-path.md 1.1-1.4).
class Arrow {
public:
    explicit Arrow(const Settings& s = Settings()) : m_set(s) {}
    void setSettings(const Settings& s) { m_set = s; m_rebuild = true; }
    const Settings& settings() const { return m_set; }
    // SpawnArrowEvent: start = the start plot's centre at the ground height, endXY = the target plot's centre (after
    // chooseWrap); the end's z is settings().targetHeight. Shows the arrow, progress 0, growing, rebuild.
    // Errors: BadLayout (a value not finite); the record is unchanged then.
    Status spawn(const float start[3], const float endXY[2]);
    // RemoveAllArrowsEvent: hides the arrow (nothing freed; the next spawn reuses it).
    void removeAll() { m_hidden = true; }
    // The per-frame update with the frame time dt in seconds: when not hidden, rebuild if asked, then while growing
    // progress += dt / animSpeed (clamped to kMaxProgress, growing ends there or at the early stop) and rebuild.
    // Errors: BadLayout (dt not finite or negative); buildMesh's.
    Status update(float dt);
    bool visible() const { return !m_hidden && m_built; }
    bool growing() const { return m_growing; }
    float progress() const { return m_progress; }
    const Mesh& mesh() const { return m_mesh; }
    uint64_t generation() const { return m_gen; }   // +1 per rebuild (the renderer uploads on change)
    const float* start() const { return m_start; }
    const float* end() const { return m_end; }

private:
    Status rebuild();
    Settings m_set;
    float m_start[3] = {}, m_end[3] = {};
    float m_progress = 0.0f;
    bool m_hidden = true, m_growing = false, m_rebuild = false, m_built = false;
    Mesh m_mesh;
    uint64_t m_gen = 0;
};

// ---- the end points (arrows-and-movement-path.md 1.2) ----

// A plot's centre without the height, from the server's height field (hexToWorld = M_h2w 0xD91030, sx, sy, ox, oy):
// sample = hex (gx - (gy >> 1), gy, 0, 1) x M_h2w, world = (sample.x sx - ox, sample.y sy - oy) (camera-and-picking.md 5;
// the same as c5r::fow::hexToWorldXY). sampleOut may be null.
void plotToWorldXY(const heightfield::HeightGrid& g, int32_t gx, int32_t gy, float out[2], float sampleOut[2] = nullptr);

// The wrap period W the spawn handler uses ([0xD90FE4], the world width): (samplesX - 1) sx, 0 when the map does not
// wrap (3583 on Tiny; 3804's grid.json wrap_period).
float worldWidth(const heightfield::HeightGrid& g);

// SpawnArrowEvent(x0, y0, x1, y1) to the record's inputs (arrows-and-movement-path.md 1.2): start = the start plot's
// centre at HeightAtGrid of the sample under it (the sample coordinates truncated: [L] 0199, 2026-11-16, the capture's
// ring 0 at plot (50, 14) of the Tiny save: z 17.95294 = the floor sample, the rounded one gives 17.57), endXY = the
// target plot's centre; then chooseWrap with `planes` when given (null: no wrap choice).
// Errors: BadLayout (the grid empty or its placement not finite, a plot outside the grid's rows), chooseWrap's.
Status spawnPoints(const heightfield::HeightGrid& g, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   const float (*planes)[4], float start[3], float endXY[2]);

// The six view-frustum planes (a, b, c, d) of a row-vector view-projection matrix (row-major), normalised, pointing
// inward: a x + b y + c z + d >= 0 inside [I: the EXE keeps its own six planes at 0xDA1510; their derivation from the
// camera was not traced]. Errors: BadLayout (a value not finite, a zero plane).
Status frustumPlanes(const float viewProj[16], float planes[6][4]);

// Steps 2 and 3 of the spawn handler: of start.x - W, start.x, start.x + W the last (in that order) whose point is inside
// every plane with a margin of 64 (distance >= -64), else start.x; then of end.x - W, end.x, end.x + W the nearest
// to the chosen start in the xy plane, with stock's quirk (end.x - W is never chosen). W <= 0 changes nothing.
// Errors: BadLayout (a value not finite).
Status chooseWrap(const float planes[6][4], float worldWidth, float start[3], float end[3]);

// ---- the draw (arrows-and-movement-path.md 1.7, arrows-and-path-line.md 3) ----

static const uint32_t kArrowVsVa = 0x0619B000u;   // ArrowShader SM5 vertex shader in the EXE's .shader section (680 B)
static const uint32_t kArrowPsVa = 0x0619B7F8u;   // ArrowShader SM5 pixel shader (560 B)
static const uint32_t kConstantBytes = 96;        // constant buffer slot 1 of VS and PS

// The 96-byte block: rows 0-3 = viewProj as given (row-major, row vectors, the EXE's View x Projection), row 4 =
// (alpha, 0, 0, 0), row 5 = (ambient, 0, 0, 0). Stock's 4.y / 4.z hold values that are not floats [L]: 0 here.
void packConstants(const float viewProj[16], const Settings& s, float out[24]);

// One pass's fixed-function state as D3D11 enum values (numbers, so this header needs no d3d11.h).
struct PassState {
    bool blendEnable;
    uint32_t srcBlend, destBlend, blendOp, srcBlendAlpha, destBlendAlpha, blendOpAlpha, writeMask;
    bool depthEnable;
    uint32_t depthWriteMask, depthFunc;
    bool stencilEnable;
    uint32_t fillMode, cullMode;
    bool frontCounterClockwise, depthClipEnable, scissorEnable, multisampleEnable;
    uint32_t topology;
};
// pass 0 = the pre-pass (blend off, write mask alpha), pass 1 = the blended draw. Any other pass returns pass 1's.
PassState passState(uint32_t pass);

struct DrawInput {
    ID3D11RenderTargetView* colour = nullptr;   // the scene's colour (stock: the back buffer)
    ID3D11DepthStencilView* depth = nullptr;    // the scene's depth (stock: D24_UNORM_S8_UINT, after the world's draws)
    uint32_t width = 0, height = 0;             // viewport
    float viewProj[16] = {};                    // the frame's View x Projection, row-major, row vectors
};

struct DrawStats {
    uint32_t draws = 0;
    uint32_t indices = 0;
    uint32_t uploads = 0;     // vertex/index uploads (once per new mesh generation)
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ArrowShader's VS and PS (kArrowVsVa, kArrowPsVa) from the .shader section of exePath (read-only), the input layout,
    // the dynamic buffers (kMaxVertices x 40 B, kMaxIndices x 4 B), the constant buffer and the two passes' states.
    // Errors: BadLayout (dev null; a blob of another stage), Io / BadMagic / Truncated (the EXE), NotFound (a blob missing
    // from the .shader section), Device / OutOfMemory. A failed init leaves the object empty.
    Status init(ID3D11Device* dev, const std::wstring& exePath);
    // The two draws of a visible arrow into in.colour with in.depth. A hidden arrow draws nothing (Ok). The mesh is
    // uploaded when the arrow's generation changed since the last draw. Sets its own IA, VS, PS, OM, RS and viewport
    // state, binds no texture or sampler, and restores nothing.
    // Errors: NotFound (init not done), BadLayout (ctx, colour or depth null; viewport 0; viewProj not finite; a mesh
    // larger than the buffers or an index past the vertices), Device.
    Status draw(ID3D11DeviceContext* ctx, const DrawInput& in, const Arrow& arrow, DrawStats* stats = nullptr);
    // The same for a mesh without an Arrow (tests, tools): uploaded on every call.
    Status drawMesh(ID3D11DeviceContext* ctx, const DrawInput& in, const Mesh& mesh, const Settings& s,
                    DrawStats* stats = nullptr);
    bool ready() const { return m_impl != nullptr; }

private:
    Status drawImpl(ID3D11DeviceContext* ctx, const DrawInput& in, const Mesh& mesh, const Settings& s, bool upload,
                    DrawStats* stats);
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace arc
} // namespace c5r
