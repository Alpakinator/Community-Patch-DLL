// Helpers of the c5r::forest draw tests (ticket 4551, epic 0431): the 3907 stock capture exports (D137, offline), the
// manifest of their tree draws, a hardware D3D11 device, render targets with readback, textures from the exports' files
// and a plain replay of a captured tree draw. Own code (the device, target and texture helpers follow
// render/tests/seamg/ice_util.cpp of this repository, issue 4306); no game bytes: the exports and the install are read at
// run time.
//
// The reference set: C5R_FOREST_REF (default F:/c5-64bit-work/0301/3907/exports, the 3907 catalogue's RenderDoc exports).
// The manifest: C5R_FOREST_MANIFEST (default render/evidence/0431/4551/manifest.txt of this checkout, found from this
// file's path). A test that needs either and does not find it FAILS (never skips).
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/forest/draw.h"
#include "c5r/status.h"

namespace c5r { namespace ftest {

std::wstring wide(const std::string& s);
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
// The reference set's root ("" after a recorded failure naming C5R_FOREST_REF).
std::string refDir();
// Reads <refDir>/<export>/<rel>; fails the test and returns false when missing or not `expectSize` bytes (0: any).
bool readRef(const std::string& exportName, const std::string& rel, std::vector<uint8_t>& out, size_t expectSize = 0);

// One texture input of a captured draw ("-" file = not bound).
struct TexRef {
    std::string file;
    uint32_t format = 0, width = 0, height = 0, depth = 0, mips = 0;
    bool bound() const { return file != "-"; }
};
struct DrawRef {
    uint32_t n = 0, view = 0;
    std::string cb1, cb7, vb, ib;
    uint32_t vbBytes = 0, ibBytes = 0, indexCount = 0;
    TexRef t[9];
};
struct ExportRef {
    std::string name;            // "units_land/L01"
    std::string vsFile, psFile;  // the captured ForestShader blobs
    std::vector<DrawRef> draws;
    uint32_t views() const;      // 1 + the largest view index
};
// The manifest's exports (empty after a recorded failure).
const std::vector<ExportRef>& manifest();
const ExportRef* findExport(const std::string& name);

float f32(const std::vector<uint8_t>& b, size_t off);
// Number of stored bytes (forestFrameStored) that differ between a and b, ignoring row 14.y when it is within `ulps`
// of b's; the first differing offset in *first, the ulp distance of row 14.y in *ulp14y.
uint32_t maskedDiff(const uint8_t* a, const uint8_t* b, uint32_t ulps, uint32_t* first, uint32_t* ulp14y);
uint32_t ulpDistance(float a, float b);

// FrameInputs from captured cb1 blocks (one per view): view = rows 0-3 (the product; proj identity), camPos = row 12,
// overlayProj = rows 4-7, fowAnim = row 8, width / height = row 9, overlay_alpha = row 10.x, the shadow direction =
// row 11, the fog colour = row 13, density = -row 14.x, fog w = row 14.w (scale: the stock 5e-5).
forest::FrameInputs capturedInputs(const std::vector<std::vector<uint8_t>>& blocks);

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ~Device();
    Status create();   // hardware, feature level 11_0
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
    void clear(ID3D11DeviceContext* ctx, const float rgba[4]);  // colour, depth 1, stencil 0
    Status read(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba);
};

// Every subresource of a 2D texture view's resource, read back tightly packed (rows of blocks for BC formats), as the
// exports store a texture.
Status readTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, std::vector<uint8_t>& out);
bool writeBmp(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);

// A texture from an export's file: 2D with tightly packed mips, or 3D (one mip). `genMips`: a render target of the
// file's mip count with mip 0 from the file and the others generated (the exports' t3 and t6 hold targets whose small
// mips are stored with a pitch; trees.md 11.1), or with `oneMip` only mip 0.
Status makeTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::string& exportName, const TexRef& t, bool genMips,
                   bool oneMip, ID3D11ShaderResourceView** srv);

// The capture's t2-t8 of one draw as views, shared by the module's draw and the replay.
struct Shared {
    ID3D11ShaderResourceView* v[9] = {};
    Shared() = default;
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
    ~Shared();
    bool make(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::string& exportName, const DrawRef& d,
              bool t3OneMip = false);
    // t3-t8 into FrameInputs (t5 / t8 at the LUT index of `entry`)
    void into(forest::FrameInputs& in, uint32_t entry) const;
};

// A plain replay of one captured tree draw: the captured VS / PS blobs, cb1 and cb7 bytes, VB / IB, t0 / t1 from the
// capture's files, t2-t8 from `sh`, stock's states as recorded in the draw's state.json (trees.md 2.4), the full target
// viewport. Not the module: the comparison's other side.
class Replay {
public:
    ~Replay();
    bool init(ID3D11Device* dev, const ExportRef& ex);
    bool draw(ID3D11DeviceContext* ctx, const Target& t, const DrawRef& d, const Shared& sh);
private:
    ID3D11Device* dev_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* il_ = nullptr;
    ID3D11Buffer* cb1_ = nullptr;
    ID3D11Buffer* cb7_ = nullptr;
    ID3D11SamplerState* samp_[9] = {};
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState* ds_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    std::string export_;
};

// Pixel difference of two RGBA8 pictures: per pixel the largest channel difference; over the pixels where either
// differs from `clear` (the drawn pixels).
struct DiffStats {
    uint64_t drawn = 0, differing = 0;
    uint32_t median = 0, p95 = 0, max = 0;
};
DiffStats diffStats(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const uint8_t clear[4],
                    std::vector<uint8_t>* diffImage);

}} // namespace c5r::ftest
