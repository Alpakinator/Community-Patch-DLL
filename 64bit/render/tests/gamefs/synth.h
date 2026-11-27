// render/tests/gamefs/synth.h - builders for synthetic packs, DDS files and installs (task 2901). Test data is made in
// memory here, never taken from the game.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <d3d11.h>

namespace synth {

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
inline void set32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
inline uint32_t cc(const char* s) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}

struct PakFile {
    std::string name;
    std::vector<uint8_t> data;
};

// A well-formed pack: index first, payloads after it in order. Pad bytes are 0xCD (the game's are garbage too).
inline std::vector<uint8_t> fpk(const std::vector<PakFile>& files) {
    std::vector<uint8_t> v;
    put32(v, 6);
    v.push_back('F'); v.push_back('P'); v.push_back('K'); v.push_back('_');
    v.push_back(0); v.push_back(0);
    put32(v, static_cast<uint32_t>(files.size()));
    std::vector<size_t> offPos;
    for (const PakFile& f : files) {
        put32(v, static_cast<uint32_t>(f.name.size()));
        v.insert(v.end(), f.name.begin(), f.name.end());
        for (size_t i = 0; i < (4 - f.name.size() % 4) % 4; ++i) v.push_back(0xCD);
        put32(v, 0);
        put32(v, 0);
        put32(v, static_cast<uint32_t>(f.data.size()));
        offPos.push_back(v.size());
        put32(v, 0);
    }
    for (size_t i = 0; i < files.size(); ++i) {
        set32(v, offPos[i], static_cast<uint32_t>(v.size()));
        v.insert(v.end(), files[i].data.begin(), files[i].data.end());
    }
    return v;
}

// Field offsets inside a DDS file (magic included).
enum : size_t { kSize = 4, kFlags = 8, kHeight = 12, kWidth = 16, kDepth = 24, kMips = 28, kPfSize = 76, kPfFlags = 80,
                kFourcc = 84, kBits = 88, kR = 92, kG = 96, kB = 100, kA = 104, kCaps = 108, kCaps2 = 112 };

struct DdsSpec {
    uint32_t w = 4, h = 4, depth = 0, mips = 1;
    uint32_t pfFlags = 0x4, fourcc = 0, bits = 0, r = 0, g = 0, b = 0, a = 0;
    uint32_t caps2 = 0;
    bool dx10 = false;
    uint32_t dxgi = 0, resDim = 3, misc = 0, arraySize = 1;
    size_t pixelBytes = 0;   // appended after the header(s)
    uint8_t fill = 0;        // pixel byte value, or 0 = a running counter
};

inline std::vector<uint8_t> dds(const DdsSpec& s) {
    std::vector<uint8_t> v(128, 0);
    std::memcpy(v.data(), "DDS ", 4);
    set32(v, kSize, 124);
    set32(v, kFlags, 0x1007 | (s.mips > 1 ? 0x20000u : 0u) | (s.depth ? 0x800000u : 0u));
    set32(v, kHeight, s.h);
    set32(v, kWidth, s.w);
    set32(v, kDepth, s.depth);
    set32(v, kMips, s.mips);
    set32(v, kPfSize, 32);
    set32(v, kPfFlags, s.dx10 ? 0x4u : s.pfFlags);
    set32(v, kFourcc, s.dx10 ? cc("DX10") : s.fourcc);
    set32(v, kBits, s.bits);
    set32(v, kR, s.r);
    set32(v, kG, s.g);
    set32(v, kB, s.b);
    set32(v, kA, s.a);
    set32(v, kCaps, 0x1000);
    set32(v, kCaps2, s.caps2);
    if (s.dx10) {
        put32(v, s.dxgi);
        put32(v, s.resDim);
        put32(v, s.misc);
        put32(v, s.arraySize);
        put32(v, 0);
    }
    for (size_t i = 0; i < s.pixelBytes; ++i) v.push_back(s.fill ? s.fill : static_cast<uint8_t>(i * 7 + 3));
    return v;
}

// Bytes of a full chain: BC blockBytes>0, else bitsPerPixel.
inline size_t chainBytes(uint32_t w, uint32_t h, uint32_t d, uint32_t mips, uint32_t slices, uint32_t blockBytes, uint32_t bpp) {
    size_t total = 0;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t mw = w >> m ? w >> m : 1, mh = h >> m ? h >> m : 1, md = d >> m ? d >> m : 1;
        if (blockBytes) total += static_cast<size_t>((mw + 3) / 4) * ((mh + 3) / 4) * blockBytes * md;
        else total += static_cast<size_t>(mw) * mh * md * bpp / 8;
    }
    return total * slices;
}

inline ID3D11Device* warpDevice() {
    ID3D11Device* dev = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, nullptr)))
        return nullptr;
    return dev;
}

} // namespace synth
