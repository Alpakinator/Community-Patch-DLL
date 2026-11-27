// Shared helpers of the c5r::ice tests (issue 4306, epic 0169): the reference set of task 4301 (D137, offline), a
// hardware D3D11 device, render targets with readback. Own code; no game bytes: the reference set and the install are
// read at run time.
//
// The reference set: C5R_ICE_REF (default F:/c5-64bit-work/0169/4301/ref, the folder with stock_load1/, stock_load2/
// and ice_probe.json; manifest render/evidence/0169/4301/manifest.json). A test that needs it and does not find it
// FAILS (never skips): set C5R_ICE_REF to the folder, e.g. `set C5R_ICE_REF=F:/c5-64bit-work/0169/4301/ref`.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/ice/ice.h"
#include "c5r/status.h"

namespace c5r { namespace itest {

// The reference set's root ("" after a recorded failure naming C5R_ICE_REF when it is missing).
std::string refDir();
// Reads a file of the reference set (relative path); fails the test and returns false when it is missing or its size
// is not `expectSize` (0: any).
bool readRef(const std::string& rel, std::vector<uint8_t>& out, size_t expectSize = 0);
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);

// One captured ice draw: the folder, its IceFrame file, whether it is the scene draw (else the reflection's).
struct CapturedDraw {
    const char* name;     // "load1 draw 171"
    const char* frame;    // relative path of the 480-byte IceFrame
    bool scene;
};
const CapturedDraw* capturedDraws(size_t* n);   // load1 171 / 86, load2 191 / 88
const char* const kVbFile = "stock_load1/buffers/43235_7e190be1.bin";   // 489,112 B
const char* const kIbFile = "stock_load1/buffers/43236_c40fee81.bin";   // 580,710 B
const uint32_t kTinyTriangles = 96785;
const float kTinyWorldWidth = 3583.0f;   // (56 * 64 - 1) * HexScale.x 1.0 [L, water-system.md 10]

// The Ice object's fields for 4301's Tiny load: Ice+0xFE80..+0xFF7F from the probe's tail (ice_probe.json, the
// stock-load1 record), +0xFF80..+0x1001F (the probe did not read them) from the scene IceFrame `frame` by the row map
// of polar-ice.md 7 (+0xFFC0 = 25 from the probe's +0xFF18 copy, +0xFFC4 = water - row 7.x).
bool tinyFields(const std::vector<uint8_t>& sceneFrame, ice::IceFields& out);
// The capture's camera and frame globals as FrameInputs for computeIceFrame: view = the block's rows 0-3 (the product,
// with proj identity), camPos = row 12, overlayProj = rows 8-11, fowAnim = row 13, fog w = row 29.w, the fog camera z =
// the scene block's row 12.z; worldWidth Tiny.
ice::FrameInputs capturedInputs(const std::vector<uint8_t>& frame, const std::vector<uint8_t>& sceneFrame);

float f32(const std::vector<uint8_t>& b, size_t off);
uint32_t u32(const std::vector<uint8_t>& b, size_t off);
// Number of stored bytes (iceFrameStored) that differ between a and b; the first differing offset in *first.
uint32_t maskedDiff(const uint8_t* a, const uint8_t* b, uint32_t* first);

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ~Device();
    Status create();   // hardware, feature level 11_0 (no WARP fallback: the test says "on a hardware device")
};

struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    uint32_t w = 0, h = 0;
    ~Target();
    Status create(ID3D11Device* dev, uint32_t w, uint32_t h);   // R8G8B8A8_UNORM + D24_UNORM_S8_UINT
    void clear(ID3D11DeviceContext* ctx);                       // colour 0, depth 1
    Status read(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba);
};

// Every subresource of a texture view's resource, read back tightly packed (rows of blocks for BC formats) in D3D11
// order: what RenderDoc's export stores for a texture (4301's textures/*.bin).
Status readTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, std::vector<uint8_t>& out);

// A 32-bit BMP of RGBA8 pixels (a diagnostic picture; never committed).
bool writeBmp(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);

std::wstring wide(const std::string& s);

// A 2D texture from tightly packed mips (uncompressed `bpp` bytes per texel, or BC with `block` bytes per 4x4 block).
Status makeTex2D(ID3D11Device* dev, const std::vector<uint8_t>& data, uint32_t w, uint32_t h, uint32_t mips, DXGI_FORMAT f,
                 uint32_t bpp, uint32_t block, ID3D11ShaderResourceView** srv);

// t7-t10 of draw 171 from the capture: the fog ramp (128 x 8, 8 mips), the FOW mask (1024^2 RGBA8; mip 0 of the frame
// start, its mips generated: the capture holds the target before draws 90-97 redraw it), the cloud base (1024^2 BC1,
// 11 mips), the TerrainFOW LUT (32^3). Read from the reference set of task 4301 (readRef).
struct Shared {
    ID3D11ShaderResourceView* v[4] = {};
    Shared() = default;
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
    ~Shared();
    bool make(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void into(ice::FrameInputs& in) const;
};

}} // namespace c5r::itest
