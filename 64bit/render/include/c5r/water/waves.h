// c5r/water/waves.h - the coast waves drawn with the EXE's WaveShader (material 92) over the EXE's wave chunks
// (issue 3606, epic 3600). Facts: research/05-graphics-composition/water-system.md sections 8.1 (the buffers), 9.4 (the
// wave object and its chunks, 3603) and 12 (this module: behaviour, the capture checks).
//
// What the EXE does [C: Wave_System render 0x643B60, update 0x6AB850, cell picker 0x5EB230, start-up 0x549C80;
// L: 3601's capture cam1_frame309938]:
//   * WaveSettings.xml (EffectData.fpk) fills the Wave_System object (0x1A0C6C8); the per-game builder 0x67C170 makes
//     chunks of at most 248 waves, each an IMMUTABLE vertex buffer (12 bytes per vertex: POSITION R16G16_FLOAT,
//     TEXCOORD0 R8G8B8A8_UNORM, BLENDINDICES0 R8G8B8A8_UINT) and an R16 index buffer, and the wave array Wave+0x5C
//     (16 bytes per wave: atlas cell u, v offset, age in seconds, unused);
//   * every frame the update adds the frame time to each wave's age; a wave past its cycle time restarts at a negative
//     age (a random delay between RestartDelayMin and RestartDelayMax) on a new random atlas page, from an LCG
//     (x * 0x343FD + 0x269EC3, modulo 0xFFFFCFEF) that starts from Wave+0x04 again at every call;
//   * per view, WaveFrame (shared id 27, 0x80 bytes) and per chunk the chunk's waves copied into the own buffer
//     (0xF80 bytes); one DrawIndexed of the whole chunk; additive blend (ONE, ONE), depth test LESS_EQUAL without
//     writes, no culling; t0 = Wave_Coastline_ALL.dds with sampler state 3 (linear, clamp).
//
// Intended use (c5r::water::Renderer of 3604, or the scene of 3607), with the tap's VPWSYS_WAVE records:
//     WaveRenderer wr;  C5R_TRY(wr.init(dev, section, files));            // once
//     WaveObject wo;    C5R_TRY(parseWaveObject(objBytes, objSize, wo));  // per load (the tap's Wave_System record)
//     C5R_TRY(wr.setChunks(chunks));  std::vector<WaveRecord> waves = <the tap's waves>;
//     per frame: updateWaves(wo, dtSeconds, waves);  C5R_TRY(wr.draw(ctx, rtv, dsv, viewProj, wo, waterZ + 1, waves, &st));
//
// Errors: every call returns a c5r::Status; nothing throws.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"

namespace c5r { namespace shaders { class ShaderSection; } }
namespace c5r { namespace gamefs { class GameFiles; } }

namespace c5r { namespace water {

const uint32_t kWaveObjectBytes = 0xB0;     // Wave_System +0x00..+0xAF, as the tap copies it
const uint32_t kWaveFrameBytes = 0x80;      // WaveFrame, shared id 27 [C]
const uint32_t kWavesPerChunk = 248;        // the own buffer holds 248 records of 16 bytes (0xF80) [C]
const uint32_t kWaveVertexBytes = 12;       // vertex format id 0x19 [L 3601]
const char* const kWaveTextureName = "Wave_Coastline_ALL.dds";   // Wave+0x68 (0x549C80) [C]

// The fields of the Wave_System object the draw and the update use. Offsets [C: 0x643B60, 0x6AB850, 0x5EB230]; the
// names follow WaveSettings.xml, whose values fill them [L: the capture's WaveFrame equals the file's values].
struct WaveObject {
    uint32_t seed = 0;            // +0x04: the update's LCG start
    uint32_t pagesU = 0;          // +0x08: CrestPagesU, atlas columns
    uint32_t pagesV = 0;          // +0x0C: CrestPagesV, atlas rows
    uint32_t pagesInUse = 0;      // +0x10: CrestPagesInUse
    float startDistance = 0;      // +0x1C: StartDistance (fraction of a hex diameter)
    float endDistance = 0;        // +0x20: EndDistance
    float cycleTime = 0;          // +0x24: CycleTime (s)
    float fadeInStart = 0, fadeInEnd = 0, fadeOutStart = 0, fadeOutEnd = 0;   // +0x28..+0x34 (s)
    float restartDelayMin = 0, restartDelayMax = 0;                           // +0x38, +0x3C (s)
    float tint[3] = {0, 0, 0};    // +0x40..+0x48: (Color / 255) ^ 2.2 [L value]
    float hexDiameter = 0;        // +0x50 (128 on the maps measured) [L value]
    uint32_t chunkCount = 0;      // +0x58
    uint32_t waveCount = 0;       // +0x60
};

// Parses the object bytes. Checks: n < 0xB0 [Truncated]; a float not finite, pagesU / pagesV / pagesInUse 0,
// pagesInUse > pagesU * pagesV, cycleTime <= 0, fadeInEnd <= fadeInStart, fadeOutEnd <= fadeOutStart (the frame
// divides by these differences), restartDelayMax < restartDelayMin, hexDiameter <= 0 [BadLayout].
Status parseWaveObject(const uint8_t* p, size_t n, WaveObject& out);

// The same fields from WaveSettings.xml's attributes (the file the EXE reads), for a run without the tap: seed 0
// (RandomSeed), hexDiameter as given, chunk and wave counts 0. Checks: a missing or unparsable attribute [BadLayout],
// then the checks of parseWaveObject.
Status waveObjectFromSettings(const std::string& xml, float hexDiameter, WaveObject& out);

// WaveFrame as 0x643B60 writes it: ViewProj, (start, end) x hexDiameter, 1 / cycleTime, planeZ, fade starts and
// inverse spans, 1 / pages, tint with w = 1; 0x68..0x6F stay 0. Errors: BadLayout (out or viewProj null, a value not
// finite, the object failing parseWaveObject's checks).
Status computeWaveFrame(const float viewProj[16], const WaveObject& w, float planeZ, uint8_t out[kWaveFrameBytes]);

struct WaveRecord {
    float u = 0, v = 0;   // atlas cell offset
    float age = 0;        // seconds; negative while waiting to restart
    float unused = 0;
};

// The atlas cell of page `index` as 0x5EB230 computes it: u = (index % pagesU) / pagesU, v = (index / pagesU) / pagesV.
void waveAtlasCell(const WaveObject& w, uint32_t index, float* u, float* v);

// One call of the update 0x6AB850. Errors: BadLayout (dt not finite or negative, the object failing its checks).
Status updateWaves(const WaveObject& w, float dt, std::vector<WaveRecord>& waves);

struct WaveChunk {
    std::vector<uint8_t> vb;      // 12-byte vertices
    std::vector<uint8_t> ib;      // R16 indices
    uint32_t firstWave = 0;       // chunk +0x0C
    uint32_t waveCount = 0;       // chunk +0x10 (<= 248)
    float bounds[4] = {0, 0, 0, 0};   // chunk +0x14..+0x20: min x, min y, max x, max y
};

struct WaveStats {
    uint32_t chunksDrawn = 0;
    uint32_t indices = 0;
};

class WaveRenderer {
public:
    WaveRenderer();
    ~WaveRenderer();
    WaveRenderer(const WaveRenderer&) = delete;
    WaveRenderer& operator=(const WaveRenderer&) = delete;

    // Creates WaveShader pass 0 from the EXE's .shader section (checked with terrainsets::validateSet), the input
    // layout, the states, both constant buffers, and Wave_Coastline_ALL.dds read through `files`. Errors: BadLayout
    // (dev null, a blob of another stage), NotFound (the set, a blob or the texture missing), the DDS parser's, Device.
    Status init(ID3D11Device* dev, const shaders::ShaderSection& sec, const gamefs::GameFiles& files);

    // Replaces the chunks (an empty vector clears them). Errors: NotFound (init not done), BadLayout (vb size not a
    // multiple of 12 or empty, ib size not a multiple of 6 or empty, an index past the vertex buffer, waveCount 0 or
    // above 248), Device.
    Status setChunks(const std::vector<WaveChunk>& chunks);

    // Draws every chunk into rtv (and dsv when not null). waves: the whole wave array (each chunk takes
    // [firstWave, firstWave + waveCount)). The context's state is changed and not restored. Errors: NotFound (init
    // not done), BadLayout (ctx or rtv null, a chunk's waves past the array, the frame's checks), Device.
    Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, const float viewProj[16],
                const WaveObject& w, float planeZ, const std::vector<WaveRecord>& waves, WaveStats* stats);

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::water
