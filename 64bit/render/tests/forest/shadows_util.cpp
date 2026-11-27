// render/tests/forest/shadows_util.cpp - see shadows_util.h (epic 0431, ticket 4552).
#include "shadows_util.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace c5r { namespace test4552 {

namespace {

template <typename T> T rd(const std::vector<uint8_t>& b, size_t at)
{
    T v;
    std::memcpy(&v, &b[at], sizeof v);
    return v;
}

bool inside(const std::vector<uint8_t>& b, uint64_t off, uint64_t bytes) { return off <= b.size() && bytes <= b.size() - off; }

} // namespace

Status readFile(const std::string& path, std::vector<uint8_t>& out)
{
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return Status::fail(Err::Io, "cannot read " + path);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return Status::Ok();
}

Status readDtap(const std::vector<uint8_t>& b, decal::DecalSet& out)
{
    out = decal::DecalSet();
    if (b.size() < 112) return Status::fail(Err::Truncated, "dtap: shorter than its header");
    if (rd<uint32_t>(b, 0) != 0x54443543u) return Status::fail(Err::BadMagic, "dtap: magic");
    if (rd<uint32_t>(b, 4) != 1u) return Status::fail(Err::BadVersion, "dtap: version");
    if (rd<uint32_t>(b, 8) != 112u || rd<uint32_t>(b, 12) != 48u) return Status::fail(Err::BadLayout, "dtap: header or record size");
    decal::DecalSet s;
    s.generation = rd<uint32_t>(b, 16);
    s.loadSeq = rd<uint32_t>(b, 20);
    const uint32_t n = rd<uint32_t>(b, 24), nn = rd<uint32_t>(b, 28), nv = rd<uint32_t>(b, 32);
    const uint32_t dOff = rd<uint32_t>(b, 40), nOff = rd<uint32_t>(b, 44), vOff = rd<uint32_t>(b, 48);
    for (int i = 0; i < 4; ++i) s.worldRect[i] = rd<float>(b, 56 + 4 * i);
    const uint32_t hashOff = rd<uint32_t>(b, 84), areaOff = rd<uint32_t>(b, 88), ax = rd<uint32_t>(b, 92),
                   ay = rd<uint32_t>(b, 96), walkOff = rd<uint32_t>(b, 100);
    if (!inside(b, dOff, 48ull * n) || !inside(b, nOff, 200ull * nn) || !inside(b, vOff, 8ull * nv) ||
        (hashOff && !inside(b, hashOff, 8ull * n)))
        return Status::fail(Err::Truncated, "dtap: a block past the end");
    for (uint32_t i = 0; i < nn; ++i) {
        const char* p = reinterpret_cast<const char*>(&b[nOff + 200ull * i + 8]);
        s.textures.emplace_back(p, strnlen(p, 192));
    }
    s.vertices.resize(nv);
    if (nv) std::memcpy(s.vertices.data(), &b[vOff], 8ull * nv);
    s.decals.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const size_t o = dOff + 48ull * i;
        decal::Decal& d = s.decals[i];
        for (int k = 0; k < 4; ++k) d.rect[k] = rd<float>(b, o + 4 * k);
        d.priority = rd<float>(b, o + 16);
        d.pass = rd<uint32_t>(b, o + 20) & 1u;
        for (int k = 0; k < 3; ++k) {
            const uint32_t t = rd<uint32_t>(b, o + 24 + 4 * k);
            d.texture[k] = t == 0xFFFFFFFFu ? decal::kNoTexture : t;
            if (t != 0xFFFFFFFFu && t >= nn) return Status::fail(Err::BadLayout, "dtap: a texture index past the names");
        }
        d.firstVertex = rd<uint32_t>(b, o + 36);
        d.vertexCount = 3 * rd<uint32_t>(b, o + 40);
        d.listOrder = rd<uint32_t>(b, o + 44);
        if (static_cast<uint64_t>(d.firstVertex) + d.vertexCount > nv) return Status::fail(Err::BadLayout, "dtap: a vertex run past the block");
        d.hash = hashOff ? rd<uint64_t>(b, hashOff + 8ull * i) : 0;
    }
    if (areaOff && ax && ay) {
        const uint64_t cells = static_cast<uint64_t>(ax) * ay;
        if (!inside(b, areaOff, (cells + 7) / 8)) return Status::fail(Err::Truncated, "dtap: the area block past the end");
        s.areaTilesX = ax;
        s.areaTilesY = ay;
        s.areaChanged.resize(static_cast<size_t>(cells));
        for (uint64_t k = 0; k < cells; ++k) s.areaChanged[k] = (b[areaOff + k / 8] >> (k % 8)) & 1u;
    }
    if (walkOff) {
        if (!inside(b, walkOff, 32ull + 16ull * n)) return Status::fail(Err::Truncated, "dtap: the walk block past the end");
        decal::DecalWalk& w = s.walk;
        w.origin[0] = rd<float>(b, walkOff);
        w.origin[1] = rd<float>(b, walkOff + 4);
        w.inverse[0] = rd<float>(b, walkOff + 8);
        w.inverse[1] = rd<float>(b, walkOff + 12);
        w.cols = rd<uint32_t>(b, walkOff + 16);
        w.rows = rd<uint32_t>(b, walkOff + 20);
        w.cell.resize(n);
        w.rank.resize(n);
        w.point.resize(2ull * n);
        for (uint32_t i = 0; i < n; ++i) {
            const size_t o = walkOff + 32 + 16ull * i;
            w.cell[i] = rd<uint32_t>(b, o);
            w.rank[i] = rd<uint32_t>(b, o + 4);
            w.point[2ull * i] = rd<float>(b, o + 8);
            w.point[2ull * i + 1] = rd<float>(b, o + 12);
        }
    }
    decal::buildIndex(s);
    if (walkOff && !decal::hasWalk(s)) return Status::fail(Err::BadLayout, "dtap: the walk block is inconsistent");
    out = std::move(s);
    return Status::Ok();
}

}} // namespace c5r::test4552
