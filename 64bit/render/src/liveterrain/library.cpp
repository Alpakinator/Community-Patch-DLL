// render/src/liveterrain/library.cpp - reader of TerrainPiecesLibrary.fxsb (ticket 4903), written from the layout
// described in research/05-graphics-composition/live-terrain.md 7.3 (our own code; D83).
#include "c5r/liveterrain/producer.h"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace c5r { namespace liveterrain {

namespace {
const size_t kRecord = 0x56C;
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
std::string num(uint64_t v) { return std::to_string(v); }
}

struct PieceLibrary::Cache {
    std::mutex mu;
    std::vector<std::vector<uint16_t>> h;          // per piece
    std::vector<std::vector<uint8_t>> b[3];        // per piece: material, height weight, material weight
};

Status PieceLibrary::open(const std::wstring& path, std::shared_ptr<const PieceLibrary>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return Status::fail(Err::Io, "piece library: cannot open the file");
    std::vector<uint8_t> bytes;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    const bool err = std::ferror(f) != 0;
    std::fclose(f);
    if (err) return Status::fail(Err::Io, "piece library: read error");
    return fromBytes(std::move(bytes), out);
}

Status PieceLibrary::fromBytes(std::vector<uint8_t> bytes, std::shared_ptr<const PieceLibrary>& out) {
    std::shared_ptr<PieceLibrary> lib(new PieceLibrary());
    lib->file_ = std::move(bytes);
    C5R_TRY(lib->parse());
    out = lib;
    return Status::Ok();
}

Status PieceLibrary::parse() {
    const uint8_t* p = file_.data();
    const size_t n = file_.size();
    size_t o = 0;
    auto need = [&](size_t k, const char* what) -> Status {
        if (o + k > n || o + k < o) return Status::fail(Err::Truncated, std::string("piece library: ") + what + " past the end");
        return Status::Ok();
    };
    C5R_TRY(need(4, "version"));
    const uint32_t ver = rd32(p);
    if (ver != 20) return Status::fail(Err::BadVersion, "piece library: version " + num(ver) + ", expected 20");
    o = 4;
    C5R_TRY(need(4, "row count"));
    nRows_ = rd32(p + o); o += 4;
    C5R_TRY(need(size_t(nRows_) * 4, "row table"));
    if (o % 4) return Status::fail(Err::BadLayout, "piece library: row table misaligned");
    rows_ = reinterpret_cast<const uint32_t*>(p + o); o += size_t(nRows_) * 4;
    C5R_TRY(need(4, "byte run count"));
    nRuns8_ = rd32(p + o); o += 4;
    C5R_TRY(need(nRuns8_, "byte runs"));
    runs8_ = p + o; o += nRuns8_;
    C5R_TRY(need(4, "u16 run count"));
    nRuns16_ = rd32(p + o); o += 4;
    C5R_TRY(need(size_t(nRuns16_) * 2, "u16 runs"));
    if (o % 2) return Status::fail(Err::BadLayout, "piece library: u16 runs misaligned");
    runs16_ = reinterpret_cast<const uint16_t*>(p + o); o += size_t(nRuns16_) * 2;
    C5R_TRY(need(4, "record count"));
    const uint32_t nRec = rd32(p + o); o += 4;
    if (nRec == 0 || nRec > 4096) return Status::fail(Err::BadLayout, "piece library: " + num(nRec) + " records");
    if (o + size_t(nRec) * kRecord != n)
        return Status::fail(Err::BadLayout, "piece library: records do not end the file (" + num(n - o) + " bytes for " + num(nRec) + ")");
    pieces_.resize(nRec);
    for (uint32_t k = 0; k < nRec; ++k) {
        const uint8_t* r = p + o + size_t(k) * kRecord;
        Piece& pc = pieces_[k];
        pc.index = rd32(r + 0x3BC);
        int32_t lv; std::memcpy(&lv, r + 0x3C8, 4);
        if (lv < 0 || lv > 16) return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " level " + std::to_string(lv));
        pc.level = lv;
        pc.flags = r[0x3D0];
        for (int t = 0; t < 4; ++t) pc.tex[t] = rd32(r + 0x3D4 + 4 * t);
        pc.region = rd16(r + 0x3E4);
        const char* nm = reinterpret_cast<const char*>(r + 0x3EA);
        pc.name.assign(nm, strnlen(nm, 64));
        int32_t w, h;
        std::memcpy(&w, r + 0x94, 4); std::memcpy(&h, r + 0x98, 4);
        if (w < 1 || w > 10 || h < 1 || h > 10)
            return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " pattern " + std::to_string(w) + "x" + std::to_string(h));
        for (int c = 0; c < w; ++c)
            for (int rr = 0; rr < h; ++rr) {
                const uint8_t* cell = r + 0x9C + 8 * (c + 10 * rr);
                Piece::Cell e;
                e.c = uint8_t(c); e.r = uint8_t(rr);
                e.flags = rd32(cell); e.continent = cell[4]; e.claimed = cell[5];
                if (e.flags != 0xFFFFFFFFu || e.claimed) pc.used.push_back(e);
            }
        if (pc.flags & 4) maxLevel_ = std::max(maxLevel_, pc.level);
        // every row of every texture must decode to exactly the texture's width inside the run arrays
        const uint32_t sz = uint32_t(pc.size());
        for (int t = 0; t < 4; ++t) {
            if (uint64_t(pc.tex[t]) + sz > nRows_)
                return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " texture " + std::to_string(t) + " rows past the table");
            for (uint32_t y = 0; y < sz; ++y) {
                uint64_t q = rows_[pc.tex[t] + y];
                uint32_t x = 0;
                while (x < sz) {
                    const uint64_t lim = t == 0 ? nRuns16_ : nRuns8_;
                    if (q + 1 >= lim) return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " row runs past the array");
                    const uint32_t len = t == 0 ? runs16_[q] : runs8_[q];
                    if (len == 0) return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " empty run");
                    if (t == 1) makes_[runs8_[q + 1]] = true;
                    x += len; q += 2;
                }
                if (x != sz) return Status::fail(Err::BadLayout, "piece library: record " + num(k) + " row overruns its width");
            }
        }
    }
    cache_ = std::make_shared<Cache>();
    cache_->h.resize(nRec);
    for (auto& b : cache_->b) b.resize(nRec);
    return Status::Ok();
}

const uint16_t* PieceLibrary::heights(uint32_t piece) const {
    std::lock_guard<std::mutex> lk(cache_->mu);
    auto& v = cache_->h[piece];
    if (v.empty()) {
        const Piece& pc = pieces_[piece];
        const uint32_t sz = uint32_t(pc.size());
        v.resize(size_t(sz) * sz);
        for (uint32_t y = 0; y < sz; ++y) {
            uint32_t q = rows_[pc.tex[0] + y], x = 0;
            while (x < sz) { const uint32_t len = runs16_[q]; const uint16_t val = runs16_[q + 1];
                for (uint32_t i = 0; i < len; ++i) v[size_t(y) * sz + x + i] = val; x += len; q += 2; }
        }
    }
    return v.data();
}

const uint8_t* PieceLibrary::bytes(uint32_t piece, int t) const {
    std::lock_guard<std::mutex> lk(cache_->mu);
    auto& v = cache_->b[t - 1][piece];
    if (v.empty()) {
        const Piece& pc = pieces_[piece];
        const uint32_t sz = uint32_t(pc.size());
        v.resize(size_t(sz) * sz);
        for (uint32_t y = 0; y < sz; ++y) {
            uint32_t q = rows_[pc.tex[t] + y], x = 0;
            while (x < sz) { const uint32_t len = runs8_[q]; const uint8_t val = runs8_[q + 1];
                std::memset(&v[size_t(y) * sz + x], val, len); x += len; q += 2; }
        }
    }
    return v.data();
}

}} // namespace c5r::liveterrain
