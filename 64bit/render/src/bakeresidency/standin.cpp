// standin.cpp - see c5r/bake/standin.h (issue 3407; moved from shell/server/standin_baker.cpp in wave 3).
#include "c5r/bake/standin.h"

#include <d3d11.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "c5r/heightfield/heightfield.h"

namespace c5r {
namespace bake {

uint64_t cacheBytes(uint32_t s) {
    const uint32_t n = cacheMips(s);
    uint64_t b = 0;
    for (uint32_t m = 0; m < n; ++m) {
        const uint64_t bw = ((s >> m) + 3) / 4;
        b += bw * bw * 8;
    }
    return b;
}

// Live-byte counter shared by the baker and the private-data objects of its textures; refcounted, outlives both.
struct StandinBaker::Counter {
    std::atomic<int64_t> bytes{0};
    std::atomic<long> refs{1};
    void addRef() { refs++; }
    void release() {
        if (--refs == 0) delete this;
    }
};

namespace {

// Attached with SetPrivateDataInterface: released by the runtime when the texture is destroyed.
class LiveTag : public IUnknown {
public:
    LiveTag(StandinBaker::Counter* c, int64_t bytes) : c_(c), bytes_(bytes) {
        c_->addRef();
        c_->bytes += bytes_;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == __uuidof(IUnknown)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;
        if (r == 0) {
            c_->bytes -= bytes_;
            c_->release();
            delete this;
        }
        return r;
    }

private:
    virtual ~LiveTag() {}
    StandinBaker::Counter* c_;
    int64_t bytes_;
    std::atomic<ULONG> refs_{1};
};

// {6C3E3407-1B2A-4E5F-9A10-3407C0DE0001}
const GUID kLiveTagGuid = {0x6c3e3407, 0x1b2a, 0x4e5f, {0x9a, 0x10, 0x34, 0x07, 0xc0, 0xde, 0x00, 0x01}};

uint16_t to565(uint32_t r, uint32_t g, uint32_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

bool isBc1(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_BC1_TYPELESS || f == DXGI_FORMAT_BC1_UNORM || f == DXGI_FORMAT_BC1_UNORM_SRGB;
}

}  // namespace

StandinBaker::StandinBaker(const StandinFaults& f) : f_(f) {}

StandinBaker::~StandinBaker() {
    if (live_) live_->release();
}

uint64_t StandinBaker::liveBytes() const { return live_ ? static_cast<uint64_t>(live_->bytes.load()) : 0; }

Status StandinBaker::init(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!dev || !ctx) return Status::fail(Err::BadLayout, "stand-in: no device or context");
    dev_ = dev;
    ctx_ = ctx;
    if (!live_) live_ = new Counter();
    return Status::Ok();
}

Status StandinBaker::setMap(const MapInputs& in) {
    if (!dev_) return Status::fail(Err::NotFound, "stand-in: init not done");
    if (!in.grid) return Status::fail(Err::BadLayout, "stand-in: no height grid");
    if (in.grid->samplesX % 64 || in.grid->samplesY % 64 || !in.grid->samplesX || !in.grid->samplesY)
        return Status::fail(Err::BadLayout, "stand-in: samples not a multiple of 64");
    tilesX_ = in.grid->samplesX / 64;
    tilesY_ = in.grid->samplesY / 64;
    haveMap_ = true;
    return Status::Ok();
}

Status StandinBaker::bake(const BakeRequest& req, BakedTile& out) {
    out = BakedTile();
    if (!haveMap_) return Status::fail(Err::NotFound, "stand-in: no map");
    if (req.tile.x >= tilesX_ || req.tile.y >= tilesY_)
        return Status::fail(Err::NotFound, "stand-in: tile (" + std::to_string(req.tile.x) + ", " + std::to_string(req.tile.y) +
                                               ") outside the map");
    const uint32_t mips = cacheMips(req.sizeClass);
    if (!mips) return Status::fail(Err::BadLayout, "stand-in: size class " + std::to_string(req.sizeClass));
    if (static_cast<int>(req.tile.x) == f_.failX && static_cast<int>(req.tile.y) == f_.failY)
        return Status::fail(Err::Unsupported, "stand-in: forced failure");
    const BakeTarget* tg[2] = {&req.t0Target, &req.t8Target};
    for (int k = 0; k < 2; ++k) {
        if (!tg[k]->array) continue;
        D3D11_TEXTURE2D_DESC d;
        tg[k]->array->GetDesc(&d);
        if (!isBc1(d.Format) || d.Width != req.sizeClass || d.Height != req.sizeClass || d.MipLevels != mips ||
            tg[k]->slice >= d.ArraySize)
            return Status::fail(Err::BadLayout, "stand-in: target array does not fit (format, size, mips or slice)");
    }
    if (f_.cpuMs > 0) {
        LARGE_INTEGER fr, a, b;
        QueryPerformanceFrequency(&fr);
        QueryPerformanceCounter(&a);
        do QueryPerformanceCounter(&b);
        while ((b.QuadPart - a.QuadPart) * 1000.0 / fr.QuadPart < f_.cpuMs);
    }
    const uint64_t one = cacheBytes(req.sizeClass);
    int own = (req.t0Target.array ? 0 : 1) + (req.t8Target.array ? 0 : 1);
    if (f_.vramBudgetMb > 0 && own && static_cast<double>(liveBytes() + own * one) > f_.vramBudgetMb * 1048576.0)
        return Status::fail(Err::OutOfMemory, "stand-in: CreateTexture2D failed 0x8007000E (E_OUTOFMEMORY)");

    const uint32_t h = req.tile.x * 73856093u ^ req.tile.y * 19349663u ^ static_cast<uint32_t>(req.contentGeneration) * 83492791u;
    const uint32_t r = 60 + (h & 0x7F), g = 70 + ((h >> 8) & 0x7F), b = 30 + ((h >> 16) & 0x3F);
    ID3D11Texture2D* tex[2] = {};
    for (int k = 0; k < 2; ++k) {
        const uint16_t c = k == 0 ? to565(r, g, b) : to565(r / 4, g / 4, b / 4);
        const uint8_t block[8] = {static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c),
                                  static_cast<uint8_t>(c >> 8), 0, 0, 0, 0};
        std::vector<std::vector<uint8_t>> data(mips);
        D3D11_SUBRESOURCE_DATA sd[8] = {};
        for (uint32_t m = 0; m < mips; ++m) {
            const uint32_t bw = ((req.sizeClass >> m) + 3) / 4;
            data[m].resize(static_cast<size_t>(bw) * bw * 8);
            for (size_t i = 0; i < data[m].size(); i += 8) std::memcpy(&data[m][i], block, 8);
            sd[m].pSysMem = data[m].data();
            sd[m].SysMemPitch = bw * 8;
        }
        if (tg[k]->array) {
            for (uint32_t m = 0; m < mips; ++m)
                ctx_->UpdateSubresource(tg[k]->array, D3D11CalcSubresource(m, tg[k]->slice, mips), nullptr, sd[m].pSysMem,
                                        sd[m].SysMemPitch, 0);
            continue;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = req.sizeClass;
        td.MipLevels = mips;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_BC1_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const HRESULT hr = dev_->CreateTexture2D(&td, sd, &tex[k]);
        if (FAILED(hr)) {
            if (tex[0]) tex[0]->Release();
            char m[96];
            std::snprintf(m, sizeof(m), "stand-in: CreateTexture2D failed 0x%08X", static_cast<unsigned>(hr));
            return Status::fail(hr == E_OUTOFMEMORY ? Err::OutOfMemory : Err::Device, m);
        }
        LiveTag* tag = new LiveTag(live_, static_cast<int64_t>(one));
        tex[k]->SetPrivateDataInterface(kLiveTagGuid, tag);
        tag->Release();
    }
    out.tile = req.tile;
    out.sizeClass = req.sizeClass;
    out.contentGeneration = req.contentGeneration;
    out.t0 = tex[0];
    out.t8 = tex[1];
    out.hasT8 = true;
    out.gpuMs = static_cast<float>(f_.cpuMs);
    bakes_++;
    return Status::Ok();
}

}  // namespace bake
}  // namespace c5r
