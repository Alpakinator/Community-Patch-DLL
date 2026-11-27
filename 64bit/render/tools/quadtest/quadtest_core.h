// render/tools/quadtest/quadtest_core.h - draw one game texture with one game VS/PS pair, no window (task 2906).
//
// Steps: a D3D11 device with no window (hardware, WARP only if no hardware adapter); the texture read through
// c5r::gamefs::GameFiles (the game's lookup order) and created with parseDds/createTexture; the VS/PS blobs of a
// shader set's pass taken from the EXE's `.shader` section (c5r::shaders::ShaderSection, read from the file at run
// time); the pass's slot-0 constant buffer filled by member NAME from the recovered layout (c5r::terrainsets); one
// quad drawn into an RGBA8 target and read back.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/shaders/shadersection.h"
#include "c5r/terrainsets/terrainsets.h"

namespace c5r { namespace quadtest {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width*height*4, R8G8B8A8
};

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    std::string adapter;   // description of the adapter, "WARP" when it fell back
    ~Device();
    Status create();       // [Device] when neither hardware nor WARP is available
};

// The game shaders of one pass of a set, checked against the set description.
struct GamePass {
    const shaders::ShaderBlob* vs = nullptr;
    const shaders::ShaderBlob* ps = nullptr;
    const terrainsets::CbLayout* cb0 = nullptr;
};
// Checks: set unknown [NotFound]; pass index missing or without an SM5 VS and PS [NotFound]; a VA not in the section
// [NotFound]; a blob of the wrong stage [BadLayout]; no slot-0 constant buffer [NotFound].
Status findPass(const shaders::ShaderSection& sec, const char* setName, uint32_t passIndex, GamePass& out);

// Fills a CPU copy of a constant buffer from the layout by member name. Checks: member not in the layout [NotFound];
// member type not Float4x4 [BadLayout]; member past the buffer end [BadLayout].
Status writeMatrix(const terrainsets::CbLayout& layout, const char* member, const float m[16], std::vector<uint8_t>& cb);

struct DrawParams {
    uint32_t width = 256, height = 256;
    float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // row-major, rows r0..r3
    float quadMin = -0.75f, quadMax = 0.75f;  // the quad's corners in POSITION space
    uint32_t samplerStateId = 0;              // terrainsets::samplerState()
};

// Draws the quad with the game pass. The VS input layout is built from the blob's own signature by the device
// (CreateInputLayout fails [Device] when the layout does not match the VS). Checks: no SRV [BadLayout]; sampler state
// unknown [NotFound]; D3D failures [Device].
Status drawGame(Device& d, const GamePass& pass, ID3D11ShaderResourceView* srv, const DrawParams& p, Image& out);
// The same quad with a trivial passthrough HLSL pair compiled at run time (the reference).
Status drawReference(Device& d, ID3D11ShaderResourceView* srv, const DrawParams& p, Image& out);

// Loads a texture by bare name through GameFiles. Checks: open failure [as GameFiles]; name not found [NotFound];
// DDS errors [as parseDds]; device errors [Device].
Status loadGameTexture(Device& d, const std::wstring& gameDir, const std::string& name, ID3D11ShaderResourceView** srv,
                       std::string* origin, std::string* format);

struct Diff { double meanAbs = 0; uint32_t maxAbs = 0; double fracOver2 = 0; uint32_t covered = 0; };
// Per-channel RGB difference over pixels where either image has alpha > 0. [BadLayout] on size mismatch.
Status compare(const Image& a, const Image& b, Diff& out);

Status writePng(const std::wstring& path, const Image& img);  // WIC, 8-bit RGBA
Image downscale(const Image& img, uint32_t w, uint32_t h);    // box filter

}} // namespace c5r::quadtest
