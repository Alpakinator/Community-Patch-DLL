// render/tools/watershot/watershot_core.h - replay of a stock capture's water draws and the comparison with the EXE's
// targets (epic 3600, issue 3604; the comparator pattern of 3403/3404, terrain-bake-comparison.md). A small library so
// that the watershot test can drive every step and its errors.
//
// Input: an export of a RenderDoc capture by issue 3601 (D:/c5-64bit-work/3600/captures/<capture>/, format version 1:
// captures/FORMAT.md, durable description research/05-graphics-composition/water-system.md section 6). Game data:
// never committed (D83). The key names this reader takes from state.json and the texture .json files are listed in
// water-system.md 10.5 ("watershot's reading of the export"); where FORMAT.md leaves a name open the reader accepts the
// D3D11 structure's own field names.
//
// Modes:
//  - replay: each draw with the capture's own shaders, constants, buffers, textures, samplers and states, into a target
//    holding the capture's "before" contents; the result against the capture's "after" contents: share of pixels
//    equal, the distribution of the rest (largest channel difference per pixel, 8 classes), median / p95 / max.
//  - module: the same, with c5r::water's own WaterFrame (computeWaterFrame) and ocean mesh (buildOceanGeometry) in
//    place of the capture's for every ocean draw; everything else as captured.
//  - frame check: the captured WaterFrame member by member against computeWaterFrame (inputs: the stock fields from
//    the install, the world, and the per-frame values the block itself carries; each member says which it is).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/water/water.h"
#include "terrainshot_core.h"

namespace c5r { namespace watershot {

using terrainshot::Json;

// A texture or render-target file pair: <name>.bin (raw subresources as the GPU stores them) + <name>.json.
struct TexFile {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0, height = 0, depth = 1, mips = 1, arraySize = 1;
    bool volume = false, cube = false, writtenInFrame = false;
    struct Sub { size_t offset = 0; uint32_t rowPitch = 0, slicePitch = 0; };
    std::vector<Sub> subs;          // mip-major within each array slice (D3D11 order)
    std::vector<uint8_t> bytes;
};
// Checks: a file missing [Io]; JSON errors [Truncated / BadLayout]; format, width or height missing [NotFound]; a size
// 0 or above 16384, a format whose block size is unknown [Unsupported], subresources past the .bin [Truncated].
Status loadTexture(const std::wstring& binPath, TexFile& out);
// Bytes per texel (or per 4x4 block for BC formats; *blockCompressed set), 0 if unknown.
uint32_t formatBytes(DXGI_FORMAT f, bool* blockCompressed);

struct DrawEntry {
    uint32_t n = 0;
    std::string system, dir;
};
struct ExportIndex {
    std::string capture;
    bool replay = false;
    std::vector<DrawEntry> draws;
};
// draws.json. Checks: missing [Io]; version other than 1 [BadVersion]; "draws" missing or an entry without n / system /
// dir [NotFound]; a dir with ".." or an absolute path [BadLayout].
Status loadIndex(const std::wstring& exportDir, ExportIndex& out);

// The comparison of one target against another of the same size and 4-byte format (RGBA8 / BGRA8, UNORM or sRGB,
// channels compared as stored). hist[k]: pixels whose largest channel difference d is 1, 2, 3-4, 5-8, 9-16, 17-32,
// 33-64, 65-255.
struct Compare {
    uint64_t total = 0, equal = 0;
    uint64_t hist[8] = {0};
    int maxDiff = 0, p50 = 0, p95 = 0;   // over the differing pixels
};
// Checks: size 0 or the buffers shorter than w*h*4 by their pitches [BadLayout].
Status compareRgba8(const uint8_t* a, uint32_t pitchA, const uint8_t* b, uint32_t pitchB, uint32_t w, uint32_t h,
                    Compare& out);
std::string describe(const Compare& c);

// One water draw's pipeline as the export gives it (state.json), with every referenced file loaded.
struct Stage {
    std::vector<uint8_t> code;
    struct Cb { uint32_t slot = 0; std::vector<uint8_t> data; };
    struct Srv { uint32_t slot = 0; DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN; TexFile tex; };
    struct Samp { uint32_t slot = 0; D3D11_SAMPLER_DESC desc; };
    std::vector<Cb> cbs;
    std::vector<Srv> srvs;
    std::vector<Samp> samps;
};
struct CapturedDraw {
    std::string call;                       // DrawIndexed, DrawIndexedInstanced, Draw, DrawInstanced
    uint32_t indexCount = 0, startIndex = 0, vertexCount = 0, startVertex = 0, instances = 1;
    int32_t baseVertex = 0;
    uint32_t topology = 0;
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    std::vector<std::string> semanticNames;  // storage for elements[i].SemanticName
    struct Vb { uint32_t slot = 0, stride = 0, offset = 0; std::vector<uint8_t> data; };
    std::vector<Vb> vbs;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t ibOffset = 0;
    std::vector<uint8_t> ib;
    Stage vs, ps;
    D3D11_BLEND_DESC blend;
    float blendFactor[4] = {0, 0, 0, 0};
    uint32_t sampleMask = 0xFFFFFFFFu;
    D3D11_DEPTH_STENCIL_DESC depth;
    uint32_t stencilRef = 0;
    bool hasDsv = false;
    D3D11_RASTERIZER_DESC raster;
    D3D11_VIEWPORT viewport;
    DXGI_FORMAT rtFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t rtWidth = 0, rtHeight = 0;
    uint32_t rtSamples = 1;                 // > 1: a multisampled target (the stock frame's is); replayed with as many samples
    bool haveBefore = false, haveAfter = false;
    TexFile before, after;
    std::vector<std::string> notes;         // what the replay cannot reproduce (e.g. no depth contents)
};
// Checks: state.json missing [Io]; a needed key missing [NotFound]; a hull / domain / geometry shader [Unsupported];
// a file outside the export [BadLayout]; then loadTexture's.
Status loadDrawState(const std::wstring& exportDir, const DrawEntry& d, CapturedDraw& out);

// The vertex shader bytecode a draw binds (state.json read, only that file loaded): for classifying the exporter's
// "candidate" draws by the EXE's own blobs (the ocean VS 0x06815A48). Errors: as loadDrawState for those keys.
Status drawVertexShader(const std::wstring& exportDir, const DrawEntry& d, std::vector<uint8_t>& code);

// The WaterFrame of an ocean draw (its VS cb1), or NotFound.
Status capturedWaterFrame(const CapturedDraw& s, std::vector<uint8_t>& out);

// The frame check. Inputs from the block (the per-frame globals the module takes as FrameInputs: ViewProj as the
// camera, OverlayProj, shadow matrices, fog, camera position, 0x220, and the clock-like fields +0xDC, +0xE4, +0xC8 solved
// from their members) are marked "input"; the rest is computed from `fields` (the stock fields) and `world`. world
// null: solved from HeightmapUV and WavePhase [I].
struct MemberCheck {
    std::string name;
    uint32_t offset = 0, bytes = 0;
    bool input = false, equal = false;
    double maxRelDiff = 0;
};
Status checkWaterFrame(const uint8_t captured[water::kWaterFrameBytes], const water::WaterFields& fields,
                       const water::World* world, std::vector<MemberCheck>& out, water::FrameInputs* used);

struct ReplayResult {
    DrawEntry draw;
    bool compared = false;
    Compare cmp;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;              // the replayed target (RGBA8 / BGRA8 as stored)
    std::vector<std::string> notes;
};
// Replays one draw. moduleFrame / moduleVb / moduleIb (any may be null): the module's WaterFrame (replaces VS and PS
// cb1), vertex bytes (replaces vertex buffer 0) and 16-bit index bytes with their draw range (replaces the index
// buffer and the call's start and count). Errors: Device; Unsupported (a target format other than 4-byte RGBA).
struct ModuleParts {
    const uint8_t* frame = nullptr;
    const std::vector<float>* vertices = nullptr;
    const std::vector<uint16_t>* indices = nullptr;
    uint32_t startIndex = 0, indexCount = 0;
    ID3D11ShaderResourceView* psSrv[14] = {};   // non-null: replaces the capture's PS view at that slot (issue 3609)
};
Status replayDraw(ID3D11Device* dev, ID3D11DeviceContext* ctx, const CapturedDraw& s, const ModuleParts* module,
                  ReplayResult& out);

}} // namespace c5r::watershot
