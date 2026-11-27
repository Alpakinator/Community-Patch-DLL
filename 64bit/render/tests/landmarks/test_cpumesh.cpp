// test_cpumesh.cpp - ticket 4879: the CPU-built meshes' store (each refusal: oversize, layout, a write after the copy)
// and resolveCpu (held, not held, a stride the material does not take, an unnamed texture, a range past the held
// buffer), on synthetic data only (no game file). The drawing itself is checked by the cityscene compare (4871's walled
// capture, W01, P6: city-renderer.md 13).
#include <cstring>
#include <vector>

#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/cpumesh.h"
#include "shell_model_tap.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

std::vector<uint8_t> indices(uint32_t n) {
    std::vector<uint8_t> b(n * 4);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t v = i % 7;
        std::memcpy(b.data() + i * 4, &v, 4);
    }
    return b;
}
std::vector<uint8_t> vertices(uint32_t n, uint32_t stride) {
    std::vector<uint8_t> b(n * stride);
    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>(i * 31 + 7);
    return b;
}

TapFrame frame(uint64_t ib, uint64_t vb, Material m, uint32_t start, uint32_t count) {
    TapFrame f;
    f.frameSeq = 3;
    f.targets.push_back(TapTarget{256, 256, 1, 28, 256, 256, 1});
    f.rows.resize(4);
    TapDraw d;
    d.material = m;
    d.key = WireKey{ib, vb, 0, 0, start, count, 0, kWireIndex32};
    d.instanceCount = 1;
    d.instRow = 0;
    d.instRows = 4;
    d.flags = VPMTAP_DRAW_DROPPED;
    f.draws.push_back(d);
    return f;
}

}  // namespace

C5R_TEST(store_hash_rule) {
    const std::vector<uint8_t> ix = indices(30), vx = vertices(12, 20);
    CHECK_EQ(geometryHash(GeomKind::Index32, ix.data(), ix.size()), VPModelHash64(ix.data() + 4, ix.size() - 4));
    CHECK_EQ(geometryHash(GeomKind::Vertex, vx.data(), vx.size()), VPModelHash64(vx.data(), vx.size()));
    CHECK_EQ(geometryHash(GeomKind::Index32, ix.data(), 4), 0ull);   // one element: no hash from element 1
}

C5R_TEST(store_add_get_remove) {
    CpuMeshStore s;
    const std::vector<uint8_t> ix = indices(30), vx = vertices(12, 32);
    const uint64_t hi = geometryHash(GeomKind::Index32, ix.data(), ix.size());
    const uint64_t hv = geometryHash(GeomKind::Vertex, vx.data(), vx.size());
    CHECK_OK(s.add(GeomKind::Index32, hi, 4, ix.data(), ix.size()));
    CHECK_OK(s.add(GeomKind::Vertex, hv, 32, vx.data(), vx.size()));
    const uint64_t g = s.generation();
    CHECK_OK(s.add(GeomKind::Vertex, hv, 32, vx.data(), vx.size()));   // held already: no copy, no change
    CHECK_EQ(s.generation(), g);
    CHECK_EQ(s.size(), 2u);
    CHECK_EQ(s.bytes(), static_cast<uint64_t>(ix.size() + vx.size()));
    CHECK(s.get(GeomKind::Vertex, hv) && s.get(GeomKind::Vertex, hv)->stride == 32);
    CHECK(!s.has(GeomKind::Index32, hv));   // the kind is part of the identity
    CHECK(s.remove(GeomKind::Vertex, hv));
    CHECK(!s.remove(GeomKind::Vertex, hv));
    CHECK_EQ(s.size(), 1u);
    CHECK(s.generation() != g);
    s.clear();
    CHECK_EQ(s.size(), 0u);
    CHECK_EQ(s.bytes(), 0ull);
}

C5R_TEST(store_refusals) {
    CpuMeshStore s;
    // oversize
    std::vector<uint8_t> big(CpuMeshStore::kMaxBytes + 20, 1);
    CHECK_ERR(s.add(GeomKind::Vertex, geometryHash(GeomKind::Vertex, big.data(), big.size()), 20, big.data(), big.size()),
              Err::Unsupported);
    CHECK_EQ(s.counts().refusedOversize, 1ull);
    // layout: an index buffer of one element, not a multiple of 4; a vertex stride the store does not take
    std::vector<uint8_t> ix = indices(1);
    CHECK_ERR(s.add(GeomKind::Index32, 1, 4, ix.data(), ix.size()), Err::BadLayout);
    ix = indices(9);
    ix.push_back(0);
    CHECK_ERR(s.add(GeomKind::Index32, 1, 4, ix.data(), ix.size()), Err::BadLayout);
    std::vector<uint8_t> vx = vertices(10, 28);
    CHECK_ERR(s.add(GeomKind::Vertex, geometryHash(GeomKind::Vertex, vx.data(), vx.size()), 28, vx.data(), vx.size()),
              Err::BadLayout);
    vx = vertices(10, 20);
    CHECK_ERR(s.add(GeomKind::Vertex, 5, 20, vx.data(), vx.size() - 1), Err::BadLayout);
    CHECK_EQ(s.counts().refusedLayout, 4ull);
    // a write after the copy: the bytes no longer match the hash taken at creation
    const uint64_t h = geometryHash(GeomKind::Vertex, vx.data(), vx.size());
    vx[7] ^= 0x40;
    CHECK_ERR(s.add(GeomKind::Vertex, h, 20, vx.data(), vx.size()), Err::BadLayout);
    CHECK_ERR(s.add(GeomKind::Vertex, 0, 20, vx.data(), vx.size()), Err::BadLayout);   // no hash
    CHECK_EQ(s.counts().refusedHash, 2ull);
    CHECK_EQ(s.size(), 0u);
}

C5R_TEST(resolve_cpu) {
    ModelLibrary lib;
    gamefs::GameFiles files;   // not opened: no texture is named
    WireIndex index(lib, files);
    CpuMeshStore s;
    const std::vector<uint8_t> ix = indices(30), v20 = vertices(12, 20), v32 = vertices(12, 32);
    const uint64_t hi = geometryHash(GeomKind::Index32, ix.data(), ix.size());
    const uint64_t h20 = geometryHash(GeomKind::Vertex, v20.data(), v20.size());
    const uint64_t h32 = geometryHash(GeomKind::Vertex, v32.data(), v32.size());
    // nothing held: the draw stays with the EXE (missing geometry)
    TapFrame f = frame(hi, h20, Material::Single, 0, 30);
    ModelScene sc;
    ResolveReport rr;
    CHECK_OK(resolve(index, lib, f, sc, &rr));
    CHECK_EQ(sc.unknownDraws, 1u);
    CpuResolveReport r;
    CHECK_OK(resolveCpu(s, index, f, sc, &r));
    CHECK_EQ(r.r32, 1u);
    CHECK_EQ(r.noGeometry, 1u);
    CHECK(sc.cpuItems.empty());
    CHECK_EQ(sc.unknownDraws, 1u);
    // held: one item, no longer unknown
    CHECK_OK(s.add(GeomKind::Index32, hi, 4, ix.data(), ix.size()));
    CHECK_OK(s.add(GeomKind::Vertex, h20, 20, v20.data(), v20.size()));
    CHECK_OK(s.add(GeomKind::Vertex, h32, 32, v32.data(), v32.size()));
    CHECK_OK(resolveCpu(s, index, f, sc, &r));
    CHECK_EQ(r.resolved, 1u);
    CHECK_EQ(sc.cpuItems.size(), 1u);
    CHECK_EQ(sc.cpuItems[0].vertexStride, 20u);
    CHECK_EQ(sc.cpuItems[0].key.indexCount, 30u);
    CHECK_EQ(sc.unknownDraws, 0u);
    CHECK(sc.unknown.empty());
    // the block programs take 32-byte vertices, material 3 20-byte ones
    TapFrame fb = frame(hi, h32, Material::Blocks, 3, 27);
    CHECK_OK(resolve(index, lib, fb, sc, &rr));
    CHECK_OK(resolveCpu(s, index, fb, sc, &r));
    CHECK_EQ(r.resolved, 1u);
    TapFrame fs = frame(hi, h32, Material::Single, 0, 30);
    CHECK_OK(resolve(index, lib, fs, sc, &rr));
    CHECK_OK(resolveCpu(s, index, fs, sc, &r));
    CHECK_EQ(r.badStride, 1u);
    CHECK(sc.cpuItems.empty());
    // an R16 key or another material is not CPU-built
    TapFrame f16 = f;
    f16.draws[0].key.flags = 0;
    CHECK_OK(resolveCpu(s, index, f16, sc, &r));
    CHECK_EQ(r.r32, 0u);
    TapFrame fi = f;
    fi.draws[0].material = Material::Instanced;
    CHECK_OK(resolveCpu(s, index, fi, sc, &r));
    CHECK_EQ(r.r32, 0u);
    // a t0 identity no file of the install gives: left with the EXE
    TapFrame ft = f;
    ft.draws[0].key.t0 = 0x1234;
    CHECK_OK(resolveCpu(s, index, ft, sc, &r));
    CHECK_EQ(r.textureUnnamed, 1u);
    CHECK(sc.cpuItems.empty());
    // a range past the held index buffer: the tap is malformed, refused whole
    TapFrame fr = frame(hi, h20, Material::Single, 10, 21);
    CHECK_ERR(resolveCpu(s, index, fr, sc, &r), Err::BadLayout);
    // checkFrame first
    TapFrame bad = f;
    bad.draws[0].target = 5;
    CHECK_ERR(resolveCpu(s, index, bad, sc, &r), Err::BadLayout);
}

C5R_TEST_MAIN
