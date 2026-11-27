// render/tools/terrainshot/terrainshot_core.h - the offline terrain picture (epic 3200, issue 3202): the ground-truth
// height field (or a C5HF tap file) and a ground-truth camera JSON drawn by c5r::terrain into PNG and float images on
// a windowless D3D11 device. A small library so that tests/terrain can drive every step and its errors.
//
// Inputs (D:/c5-64bit-work/i2-ground-truth/README.md): heightmap_u16le_<X>x<Y>.raw (u16 little-endian, row 0 first),
// heightmap_info.json (samples_x, samples_y), grid_constants.json (wrap_x, grid_sx, grid_sy, grid_height_scale,
// grid_ox, grid_oy, M_h2w, heightmap_header_hex: its f32 at +4 is the water height 0xD91274); camera_*.json from
// shell/tools/gt_reader.py camera (proj, views[0], inv_views[0], backbuffer).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <d3d11.h>
#include "c5r/status.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/terrain/terrain.h"

namespace c5r { namespace terrainshot {

// ---- a minimal JSON reader (objects, arrays, numbers, strings, true/false/null) ----
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;
    const Json* get(const std::string& key) const;  // nullptr when not an object or the key is missing
};
// Checks: empty or truncated text [Truncated]; a syntax error, nesting deeper than 64, trailing text [BadLayout].
Status parseJson(const std::string& text, Json& out);
Status readJsonFile(const std::wstring& path, Json& out);  // + Io

// Ground-truth folder (the three files above). Checks: a file missing [Io]; a key missing or of the wrong type
// [NotFound]; samples not multiples of 64 or above 256 plots, raw size != X*Y*2, header hex shorter than 8 bytes
// [BadLayout]; then the checks of setHeightField happen in the renderer.
Status loadGroundTruth(const std::wstring& dir, heightfield::HeightGrid& out);
// A C5HF tap file (c5r::heightfield::readTap): Io, then readTap's errors (Truncated, BadMagic, BadVersion, BadLayout).
Status loadTapFile(const std::wstring& path, heightfield::HeightGrid& out);

// Camera JSON: proj, views[view], inv_views[view] (optional), backbuffer [w, h]. Checks: key missing or not a 4x4 of
// numbers [NotFound]; view outside 0..num_views-1 [BadLayout]; a value not finite or the size 0 [BadLayout].
Status loadCamera(const std::wstring& path, int view, terrain::Camera& out);

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    std::string adapter;
    ~Device();
    Status create(bool warp);  // [Device] when neither hardware (unless warp) nor WARP gives a feature level 11_0 device
};

struct ShotResult {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;    // colour, R8G8B8A8
    std::vector<float> world;     // when asked: x, y, z, w per pixel (w 1 terrain, 2 water, 0 nothing)
    terrain::FrameStats stats;
    double gpuMs = -1;
};

// Draws one shot: colour into an RGBA8 target (cleared to clearRgba, default the D33 clear colour 84,84,84) and, with
// wantWorld, a second draw with ColorSource::WorldPos into a float target. Errors: the renderer's.
Status renderShot(Device& d, terrain::Renderer& r, const terrain::Camera& cam, const terrain::Options& o, bool wantWorld,
                  ShotResult& out);

// PNG (R8G8B8A8, WIC). Checks: size 0 or rgba.size() != w*h*4 [BadLayout]; WIC [Device]; file [Io].
Status writePngRgba(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba);
// Raw little-endian float32 xyzw rows plus PATH.json {"width", "height", "channels": 4, "format": "f32le"}.
Status writeWorld(const std::wstring& path, uint32_t w, uint32_t h, const std::vector<float>& xyzw);

}} // namespace c5r::terrainshot
