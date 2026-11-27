// c5r/water/currents.h - the "Water Currents" map (WaterShader t5 "WaterBlendMap" of the ocean passes) computed in the
// server with the EXE's own WorldCurrents program (issue 3602, epic 3600). Owner: issue 3602. Facts:
// research/05-graphics-composition/water-system.md section 8 (behaviour description of the pass, its inputs and
// target, with [C]/[I] marks).
//
// What the EXE does [C, decompile of 0x53A9F0, 0x444870, 0x5F06A0, 0x6B8546; blobs of the .shader section]:
//   * the target: "Water Currents" 0xDA12C4, 256 x 256, R8G8B8A8 (typeless 0x1b, UNORM view 0x1c, sRGB view 0x1d),
//     1 mip, created by TerrainSystem::Startup only when the water-quality global [0xD90F68] > 0;
//   * computed at load (Water::Startup 0x5F06A0 and TerrainSystem's 0x4B2050 -> 0x444870), not per frame: one draw of
//     WorldCurrents (material 93) **pass 3** (VS 0x0682D730, PS 0x068306D0) over a full-screen quad, constant slot 1 =
//     shared buffer 43 (CurrentsFrame, 0x40 bytes): (float [0xD91264], float [0xD91268], 0, 0), (Water+0x64 = 0.1),
//     (Water+0x68 = 2.0), (Water+0x60 = 0.1); t0 = draw-object texture handle 0 with sampler state 0;
//   * the pass writes, per texel at uv: n = gradient noise of uv on a floor(size / Water+0x68) cell lattice;
//     noiseColour = (0.5 - 0.5 * a * n.x, 0.5 + 0.5 * a * n.y, 0, 1) with a = Water+0x64; g = Sobel gradient of t0.r
//     at a step of 1 / size, times Water+0x60; flowColour = (0.5 - 0.5 g.y, 0.5 + 0.5 g.x, 0, |g|);
//     result = lerp(noiseColour, flowColour, min(|g|, 1)).
//   [L, 3601's Tiny capture, water-system.md 8.6]: Size = the grid's plots, the server's quad orientation and the UNORM
//   view are the EXE's: with a null t0 the server's map equals stock in 91.8 % of the texels and in 99.985 % of those
//   without a flow term. [I, open]: the texture behind renderer handle 0 (t0): its gradients make a corner around
//   uv (0, 0) (8.2 % of the texels, up to 35 steps) that a null t0 leaves out.
//
// Intended use by c5r::water::Renderer (issue 3604), once per height-field change:
//     c5r::water::Currents cur;                                  // a member of Renderer::Impl
//     C5R_TRY(cur.init(dev, section));                           // section: the EXE's ShaderSection, opened once
//     c5r::water::CurrentsParams cp;
//     C5R_TRY(c5r::water::currentsParamsFromGrid(grid, cp));     // in Renderer::setHeightField
//     C5R_TRY(cur.compute(ctx, cp, nullptr));                    // nullptr: a null t0 (stock's t0 is open, above)
//     ... bind cur.srv() at PS t5 (sampler state 0) for the ocean passes 0/2/3/5.
//
// Errors: every call returns a c5r::Status; nothing throws.
#pragma once
#include <cstdint>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"

namespace c5r { namespace shaders { class ShaderSection; } }

namespace c5r { namespace water {

const uint32_t kCurrentsMapSize = 256;       // "Water Currents" 256 x 256 [C]
const uint32_t kCurrentsFrameBytes = 0x40;   // CurrentsFrame, shared id 43 [C]
const uint32_t kCurrentsPass = 3;            // the WorldCurrents pass the EXE draws [C]

struct CurrentsParams {
    float sizeX = 0, sizeY = 0;       // CurrentsFrame register 0 .xy = float [0xD91264], float [0xD91268]: the grid's
                                      // plots (Tiny 56 x 40) [C read, L by the capture fit, water-system.md 8.6]
    float noiseAmplitude = 0.1f;      // register 1 .x = Water+0x64 (0.1 at start-up) [C, L]
    float noiseCell = 2.0f;           // register 2 .x = Water+0x68 (2.0): 28 x 20 cells on Tiny [C, L]
    float gradientScale = 0.1f;       // register 3 .x = Water+0x60 (0.1) [C, L]
};

// The parameters for a height field: sizeX / sizeY = the grid's plots (mapW, mapH; the noise lattice of 3601's Tiny
// capture fits only these, 8.6), the rest the start-up values. Errors: BadLayout (mapW or mapH 0).
Status currentsParamsFromGrid(const heightfield::HeightGrid& g, CurrentsParams& out);

// The 0x40-byte CurrentsFrame block as 0x53A9F0 writes it (unwritten members 0). Errors: BadLayout (out null, a value
// not finite, sizeX / sizeY / noiseCell not > 0, or sizeX / noiseCell or sizeY / noiseCell below 1: a lattice of
// zero cells divides by zero in the shader).
Status computeCurrentsFrame(const CurrentsParams& p, uint8_t out[kCurrentsFrameBytes]);

// CPU model of the pass, written from the behaviour description in water-system.md section 8 (not from the EXE's
// code): for texel (x, y) at uv = ((x + 0.5) / 256, (y + 0.5) / 256), with source(u, v) = the red channel the pass
// samples (bilinear, wrap, at the given resolution; nullptr = 0 everywhere). out: 256 * 256 RGBA floats in [0, 1]
// before quantisation. Used by the tests as the independent check of the GPU path. Errors: those of
// computeCurrentsFrame; BadLayout (source given with a size of 0).
Status currentsModel(const CurrentsParams& p, const float* source, uint32_t srcW, uint32_t srcH, std::vector<float>& out);

// Quantises model output as the UNORM8 target stores it: nearest, a tie rounds down (the workstation GPU also rounds
// fractions up to about 0.56 down, inside D3D11's 0.6-unit conversion tolerance: water-system.md 8.6).
void quantiseUnorm8(const std::vector<float>& in, std::vector<uint8_t>& out);

class Currents {
public:
    Currents();
    ~Currents();
    Currents(const Currents&) = delete;
    Currents& operator=(const Currents&) = delete;

    // Creates WorldCurrents pass 3 (VS + PS from the EXE's .shader section, checked with terrainsets::validateSet),
    // the full-screen quad, CurrentsFrame, sampler state 0 and the 256 x 256 R8G8B8A8 target.
    // Errors: BadLayout (dev null), NotFound (WorldCurrents or a blob missing), BadLayout (a blob of another stage or
    // failing validation), Device.
    Status init(ID3D11Device* dev, const shaders::ShaderSection& sec);

    // Draws the map. source: the SRV for t0 (nullptr = a null view, as the EXE's handle 0 is assumed to be).
    // The context's state is changed and not restored. Errors: NotFound (init not done), BadLayout (ctx null, the
    // params as computeCurrentsFrame), Device.
    Status compute(ID3D11DeviceContext* ctx, const CurrentsParams& p, ID3D11ShaderResourceView* source);

    ID3D11ShaderResourceView* srv() const;   // the UNORM view, for WaterShader t5
    ID3D11Texture2D* texture() const;
    uint64_t generation() const;             // increments with every compute()

    // Copies the target to the CPU: 256 rows of 256 RGBA8 texels, tightly packed. Errors: NotFound (nothing computed),
    // BadLayout (ctx null), Device.
    Status readback(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba8);

    void release();

private:
    struct Impl;
    Impl* p_ = nullptr;
};

}} // namespace c5r::water
