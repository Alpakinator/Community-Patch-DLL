// Shared helpers of the c5r::water tests (issue 3604): a windowless D3D11 device (hardware, WARP if none), a render
// target with CPU readback, synthetic height fields and cameras. Own code; no game bytes.
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"
#include "c5r/terrain/terrain.h"

namespace c5r { namespace wtest {

struct Device {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    std::string adapter;
    ~Device();
    Status create(bool warp);
};

struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    uint32_t w = 0, h = 0;
    ~Target();
    Status create(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT f = DXGI_FORMAT_R8G8B8A8_UNORM);
    // RGBA8 pixels, row-major from the top-left
    Status read(ID3D11DeviceContext* ctx, std::vector<uint8_t>& rgba);
};

// mapW x mapH plots, every sample at u16 h (sea floor 0 = deep water everywhere).
heightfield::HeightGrid flatGrid(uint32_t mapW, uint32_t mapH, uint16_t h, bool wrap = false);

// A camera at (cx, cy, height) looking straight down (+y up on screen), RH perspective, vertical fov 30 degrees.
terrain::Camera downCamera(float cx, float cy, float height, uint32_t w, uint32_t h);

}} // namespace c5r::wtest
