// c5r/bake/standin.h - a stand-in c5r::bake::TileBaker (issue 3407, epic 3400): one flat BC1 colour per tile, for the
// residency's tests and for runs without the install. Not the EXE's picture (COMMON 25 / D66): a stand-in.
//
// Keeps the contract of baker.h: t0 and t8 are BC1 of sizeClass^2 with cacheMips(sizeClass) mips (43,680 bytes each at
// 256). With a BakeTarget the colour is written into that array slice (UpdateSubresource, every mip) and no texture is
// returned; without one, immutable textures are returned. gpuMs is filled with the simulated cost (cpuMs).
// Test seams (StandinFaults): a tile whose bake fails (Unsupported), a video-memory budget for the returned textures
// (beyond it: OutOfMemory), a busy wait per bake.
#pragma once
#include <cstdint>

#include "c5r/bake/baker.h"
#include "c5r/bake/post.h"   // cacheMips

namespace c5r {
namespace bake {

// Bytes of one BC1 cache of that size class (43,680 at 256); 0 for an unknown size.
uint64_t cacheBytes(uint32_t sizeClass);

struct StandinFaults {
    int failX = -1, failY = -1;   // the tile whose bake fails (-1: none)
    double vramBudgetMb = 0;      // > 0: OutOfMemory once the stand-in's own live textures would exceed this
    double cpuMs = 0;             // busy wait per bake, reported as gpuMs
};

class StandinBaker : public TileBaker {
public:
    explicit StandinBaker(const StandinFaults& f);
    ~StandinBaker() override;
    StandinBaker(const StandinBaker&) = delete;
    StandinBaker& operator=(const StandinBaker&) = delete;
    // BadLayout: dev or ctx null.
    Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx) override;
    // NotFound before init; BadLayout: grid null or samples not a multiple of 64.
    Status setMap(const MapInputs& in) override;
    // NotFound: no map, tile outside; BadLayout: size class; Unsupported: the fail tile; OutOfMemory: the budget;
    // BadLayout: a target array that is not BC1 of the size class or a slice past its end; Device: D3D11.
    Status bake(const BakeRequest& req, BakedTile& out) override;

    uint64_t liveBytes() const;   // bytes of the returned textures not yet destroyed
    uint64_t bakes() const { return bakes_; }
    struct Counter;

private:
    StandinFaults f_;
    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    uint32_t tilesX_ = 0, tilesY_ = 0;
    bool haveMap_ = false;
    uint64_t bakes_ = 0;
    Counter* live_ = nullptr;
};

}  // namespace bake
}  // namespace c5r
