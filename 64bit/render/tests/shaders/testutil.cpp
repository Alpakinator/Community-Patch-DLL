// Helpers for the shaders tests (task 2902). See testutil.h.
#include "testutil.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <cstdio>

namespace c5r { namespace test {

std::vector<uint8_t> compileHlsl(const char* src, const char* entry, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT h = D3DCompile(src, std::strlen(src), "test.hlsl", nullptr, nullptr, entry, target, 0, 0, &code, &err);
    std::vector<uint8_t> out;
    if (FAILED(h) || !code) {
        std::fprintf(stderr, "D3DCompile %s failed 0x%08lx: %s\n", target, static_cast<unsigned long>(h),
                     err ? static_cast<const char*>(err->GetBufferPointer()) : "");
    } else {
        const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
        out.assign(p, p + code->GetBufferSize());
    }
    if (code) code->Release();
    if (err) err->Release();
    return out;
}

std::vector<uint8_t> stripLikeExe(const std::vector<uint8_t>& blob) {
    ID3DBlob* s = nullptr;
    std::vector<uint8_t> out;
    if (SUCCEEDED(D3DStripShader(blob.data(), blob.size(),
                                 D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO |
                                     D3DCOMPILER_STRIP_TEST_BLOBS | D3DCOMPILER_STRIP_PRIVATE_DATA,
                                 &s)) && s) {
        const uint8_t* p = static_cast<const uint8_t*>(s->GetBufferPointer());
        out.assign(p, p + s->GetBufferSize());
    }
    if (s) s->Release();
    return out;
}

std::vector<uint8_t> makeContainer(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& chunks) {
    std::vector<uint8_t> c;
    put32(c, shaders::fourcc('D', 'X', 'B', 'C'));
    c.resize(20, 0);
    put32(c, 1); // version 1.0
    put32(c, 0); // total, patched below
    put32(c, static_cast<uint32_t>(chunks.size()));
    const size_t offAt = c.size();
    c.resize(c.size() + 4 * chunks.size(), 0);
    for (size_t i = 0; i < chunks.size(); ++i) {
        set32(c, offAt + 4 * i, static_cast<uint32_t>(c.size()));
        put32(c, chunks[i].first);
        put32(c, static_cast<uint32_t>(chunks[i].second.size()));
        c.insert(c.end(), chunks[i].second.begin(), chunks[i].second.end());
        while (c.size() % 4) c.push_back(0);
    }
    set32(c, 24, static_cast<uint32_t>(c.size()));
    shaders::dxbcFixChecksum(c.data(), c.size());
    return c;
}

std::vector<uint8_t> makeProgram(uint32_t type, uint32_t major, uint32_t minor, const std::vector<uint32_t>& tokens) {
    std::vector<uint8_t> p;
    put32(p, (type << 16) | (major << 4) | minor);
    put32(p, static_cast<uint32_t>(2 + tokens.size()));
    for (uint32_t t : tokens) put32(p, t);
    return p;
}

size_t chunkDataOffset(const std::vector<uint8_t>& c, uint32_t f) {
    const uint32_t n = get32(c, 28);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t off = get32(c, 32 + 4 * i);
        if (get32(c, off) == f) return off + 8;
    }
    return 0;
}

std::vector<uint8_t> buildPe(const std::vector<SynthSection>& secs, uint32_t imageBase, uint32_t timeDateStamp,
                             uint32_t sizeOfImage) {
    std::vector<uint8_t> f(0x400, 0);
    f[0] = 'M';
    f[1] = 'Z';
    set32(f, 0x3C, 0x40);
    set32(f, 0x40, 0x00004550);
    f[0x44] = 0x4C; f[0x45] = 0x01; // Machine i386
    f[0x46] = static_cast<uint8_t>(secs.size());
    set32(f, 0x48, timeDateStamp);
    f[0x54] = 224; // SizeOfOptionalHeader
    f[0x56] = 0x02; f[0x57] = 0x01; // Characteristics
    const size_t opt = 0x58;
    f[opt] = 0x0B; f[opt + 1] = 0x01; // PE32
    set32(f, opt + 28, imageBase);
    set32(f, opt + 56, sizeOfImage);
    size_t sec = opt + 224;
    uint32_t raw = 0x400;
    for (const SynthSection& s : secs) {
        const uint32_t rawSize = static_cast<uint32_t>((s.data.size() + 0x1FF) & ~static_cast<size_t>(0x1FF));
        for (size_t i = 0; i < s.name.size() && i < 8; ++i) f[sec + i] = static_cast<uint8_t>(s.name[i]);
        set32(f, sec + 8, s.virtualSize);
        set32(f, sec + 12, s.rva);
        set32(f, sec + 16, rawSize);
        set32(f, sec + 20, rawSize ? raw : 0);
        set32(f, sec + 36, 0x40000040);
        sec += 40;
        if (rawSize) {
            f.resize(raw + rawSize, 0);
            std::memcpy(f.data() + raw, s.data.data(), s.data.size());
            raw += rawSize;
        }
    }
    return f;
}

}} // namespace c5r::test
