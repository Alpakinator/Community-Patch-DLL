// tap.cpp - the tap frame's file form (.mtap = 4874's frame image, shell/common/shell_model_tap.h), the wire key and
// texture identity, the library's wire index, resolve() and the scene file form .c5models (ticket 4873, epic 0561).
// Own code from the knowledge base's description (D83): city-renderer.md 2.2, 2.4, 3.2, 5.4-5.6; landmark-renderer.md.
#include "c5r/landmarks/tap.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <tuple>

#include "c5models/art.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "shell_model_scene.h"
#include "shell_model_tap.h"

namespace c5r { namespace landmarks {

// ---------------------------------------------------------------------------------------------- materials
const char* materialName(Material m) {
    switch (m) {
    case Material::Single: return "single(3)";
    case Material::Instanced: return "instanced(4)";
    case Material::Blocks: return "blocks(5)";
    case Material::Skinned: return "skinned(6)";
    }
    return "?";
}

bool materialFromClass(uint32_t cls, Material& out) {
    switch (cls) {
    case VPMODEL_CLASS_BLOCK: out = Material::Blocks; return true;
    case VPMODEL_CLASS_SINGLE: out = Material::Single; return true;
    case VPMODEL_CLASS_INST: out = Material::Instanced; return true;
    case VPMODEL_CLASS_SKINNED: out = Material::Skinned; return true;
    default: return false;
    }
}

uint8_t classOf(Material m) {
    switch (m) {
    case Material::Blocks: return VPMODEL_CLASS_BLOCK;
    case Material::Single: return VPMODEL_CLASS_SINGLE;
    case Material::Instanced: return VPMODEL_CLASS_INST;
    case Material::Skinned: return VPMODEL_CLASS_SKINNED;
    }
    return VPMODEL_CLASS_NONE;
}

uint32_t rowsPerInstance(Material m) {
    return m == Material::Skinned ? VPSHELL_MODELTAP_PALETTE_ROWS : VPSHELL_MODELTAP_INST_ROWS;
}

Material partMaterial(ModelClass fileClass, bool skinned) {
    if (skinned) return Material::Skinned;
    if (fileClass == ModelClass::CityBlock) return Material::Blocks;
    if (fileClass == ModelClass::CityPiece) return Material::Single;
    return Material::Instanced;
}

namespace {
// The vertex form a material's program reads (landmark-renderer.md 2.1 [C]).
bool formFits(Material m, const FileLayout& lay, const Part& p) {
    switch (m) {
    case Material::Skinned: return p.skinned;
    case Material::Blocks: return !p.skinned && lay.form == VertexForm::Float32;
    case Material::Single:
    case Material::Instanced: return !p.skinned && lay.form == VertexForm::Half20;
    }
    return false;
}
}  // namespace

// ---------------------------------------------------------------------------------------------- hashes
bool WireKey::operator<(const WireKey& o) const {
    return std::tie(ib, vb, startIndex, indexCount, baseVertex, flags, t0, t1) <
           std::tie(o.ib, o.vb, o.startIndex, o.indexCount, o.baseVertex, o.flags, o.t0, o.t1);
}

uint64_t wireHash(const void* data, size_t bytes) { return VPModelHash64(data, bytes); }

Status wireIndexHash(const void* indices, size_t bytes, uint32_t indexBytes, uint64_t& out) {
    out = 0;
    if (indexBytes != 2 && indexBytes != 4) return Status::fail(Err::BadLayout, "index size not 2 or 4");
    if (bytes % indexBytes) return Status::fail(Err::BadLayout, "index buffer size not a multiple of the index size");
    if (bytes < 2u * indexBytes) return Status::fail(Err::BadLayout, "index buffer of fewer than two elements");
    out = VPModelHash64(static_cast<const uint8_t*>(indices) + indexBytes, bytes - indexBytes);
    return Status::Ok();
}

uint64_t textureIdentity(uint32_t dxgiFormat, uint32_t width, uint32_t height, uint32_t mipLevels,
                         const std::vector<TextureLevel>& mips) {
    if (!width || !height || !mipLevels) return 0;
    const uint32_t m = VPModelTextureMip(width, height, mipLevels);
    if (m >= mips.size() || !mips[m].data) return 0;
    uint32_t bw = 1;
    const uint32_t bb = VPModelFormatBlock(dxgiFormat, &bw);
    if (!bb) return 0;
    const uint32_t mw = std::max(1u, width >> m), mh = std::max(1u, height >> m);
    const uint32_t rowBytes = (mw + bw - 1) / bw * bb, rows = (mh + bw - 1) / bw;
    const uint32_t pitch = mips[m].rowPitch ? mips[m].rowPitch : rowBytes;
    if (pitch < rowBytes || static_cast<uint64_t>(pitch) * (rows - 1) + rowBytes > mips[m].bytes) return 0;
    return VPModelTextureHash(mips[m].data, pitch, mw, mh, dxgiFormat);
}

// ---------------------------------------------------------------------------------------------- the frame
Status checkFrame(const TapFrame& f) {
    if (f.draws.size() > VPSHELL_MODELTAP_MAX_DRAWS) return Status::fail(Err::BadLayout, "more draws than the bound");
    if (f.consts.size() > VPSHELL_MODELTAP_MAX_CONSTS) return Status::fail(Err::BadLayout, "more consts than the bound");
    if (f.targets.size() > VPSHELL_MODELTAP_MAX_TARGETS) return Status::fail(Err::BadLayout, "more targets than the bound");
    const uint64_t rows = f.rows.size();
    for (size_t i = 0; i < f.consts.size(); ++i) {
        const TapConst& c = f.consts[i];
        if (c.rows == 0 || c.rows * 16ull > VPSHELL_MODELTAP_CB_MAX_BYTES ||
            static_cast<uint64_t>(c.firstRow) + c.rows > rows)
            return Status::fail(Err::BadLayout, "const " + std::to_string(i) + " outside the rows or too big");
    }
    for (size_t i = 0; i < f.draws.size(); ++i) {
        const TapDraw& d = f.draws[i];
        // ticket 4884: the draw's name made only for a refusal (was a string per draw; readTap and resolve run every frame)
        auto who = [i]() { return "draw " + std::to_string(i); };
        if (d.target >= f.targets.size()) return Status::fail(Err::BadLayout, who() + ": target out of range");
        if (d.instanceCount == 0 || d.instanceCount > VPSHELL_MODELTAP_MAX_INSTANCES)
            return Status::fail(Err::BadLayout, who() + ": instance count 0 or above the bound");
        if (d.material == Material::Single && d.instanceCount != 1)
            return Status::fail(Err::BadLayout, who() + ": a single-model draw with more than one instance");
        if (d.instRows != rowsPerInstance(d.material))
            return Status::fail(Err::BadLayout, who() + ": rows per instance not those of its class");
        if (static_cast<uint64_t>(d.instRow) + static_cast<uint64_t>(d.instRows) * d.instanceCount > rows)
            return Status::fail(Err::BadLayout, who() + ": instance rows outside the row block");
        if ((d.vsCb1 != kNoConst && d.vsCb1 >= f.consts.size()) || (d.psCb1 != kNoConst && d.psCb1 >= f.consts.size()))
            return Status::fail(Err::BadLayout, who() + ": constant reference out of range");
        if (d.key.indexCount == 0) return Status::fail(Err::BadLayout, who() + ": index count 0");
    }
    return Status::Ok();
}

namespace {

struct Region { uint64_t off, bytes; const char* what; };

Status checkRegions(std::vector<Region> r, uint64_t headerBytes, uint64_t imageBytes) {
    std::sort(r.begin(), r.end(), [](const Region& a, const Region& b) { return a.off < b.off; });
    uint64_t end = headerBytes;
    for (const Region& x : r) {
        if (x.bytes == 0) continue;
        if (x.off % 16) return Status::fail(Err::BadLayout, std::string(x.what) + " offset not 16-byte aligned");
        if (x.off < end) return Status::fail(Err::BadLayout, std::string(x.what) + " overlaps the header or another array");
        if (x.off + x.bytes > imageBytes) return Status::fail(Err::BadLayout, std::string(x.what) + " past the image's end");
        end = x.off + x.bytes;
    }
    return Status::Ok();
}

uint32_t align16(uint64_t v) { return static_cast<uint32_t>((v + 15) & ~15ull); }

}  // namespace

Status readTap(const uint8_t* data, size_t size, TapFrame& out) {
    out = TapFrame();
    if (!data || size < 8) return Status::fail(Err::Truncated, "shorter than the frame header's magic and version");
    VPModelTapFrame h;
    std::memcpy(&h, data, std::min(size, sizeof h));
    if (h.magic != VPSHELL_MODELTAP_MAGIC) return Status::fail(Err::BadMagic, "not a model tap frame (magic)");
    if (h.version != VPSHELL_MODELTAP_VERSION)
        return Status::fail(Err::BadVersion, "frame version " + std::to_string(h.version) + " (this reader: 1)");
    if (size < sizeof h) return Status::fail(Err::Truncated, "shorter than the frame header");
    if (h.headerBytes != sizeof(VPModelTapFrame) || h.drawBytes != sizeof(VPModelTapDraw))
        return Status::fail(Err::BadLayout, "header or draw record size not this layout's");
    if (h.imageBytes < h.headerBytes) return Status::fail(Err::BadLayout, "image smaller than its header");
    if (h.imageBytes > size) return Status::fail(Err::Truncated, "image bytes past the data");
    if (h.drawCount > VPSHELL_MODELTAP_MAX_DRAWS || h.constCount > VPSHELL_MODELTAP_MAX_CONSTS ||
        h.targetCount > VPSHELL_MODELTAP_MAX_TARGETS || h.rowCount > h.imageBytes / 16u)
        return Status::fail(Err::BadLayout, "a count above its bound");
    C5R_TRY(checkRegions({{h.drawOffset, static_cast<uint64_t>(h.drawCount) * sizeof(VPModelTapDraw), "draws"},
                          {h.rowOffset, static_cast<uint64_t>(h.rowCount) * 16u, "rows"},
                          {h.constOffset, static_cast<uint64_t>(h.constCount) * sizeof(VPModelTapConst), "consts"},
                          {h.targetOffset, static_cast<uint64_t>(h.targetCount) * sizeof(VPModelTapTarget), "targets"}},
                         h.headerBytes, h.imageBytes));
    if (VPModelHash64(data + h.headerBytes, h.imageBytes - h.headerBytes) != h.fnv1a64)
        return Status::fail(Err::BadLayout, "the frame's hash does not match its bytes");
    out.frameSeq = h.frameSeq;
    out.presentCount = h.presentCount;
    out.setGeneration = h.setGeneration;
    out.latchedBits = h.latchedBits;
    out.wantBits = h.wantBits;
    out.dropped[0] = h.dropped[0];
    out.dropped[1] = h.dropped[1];
    out.passedTapped = h.passedTapped;
    out.truncated = h.truncated;
    out.rows.resize(h.rowCount);
    if (h.rowCount) std::memcpy(out.rows.data(), data + h.rowOffset, h.rowCount * 16ull);
    out.targets.resize(h.targetCount);
    for (uint32_t i = 0; i < h.targetCount; ++i) {
        VPModelTapTarget t;
        std::memcpy(&t, data + h.targetOffset + i * sizeof t, sizeof t);
        out.targets[i] = TapTarget{t.width, t.height, t.samples, t.format, t.dsvWidth, t.dsvHeight, t.draws};
    }
    out.consts.resize(h.constCount);
    for (uint32_t i = 0; i < h.constCount; ++i) {
        VPModelTapConst c;
        std::memcpy(&c, data + h.constOffset + i * sizeof c, sizeof c);
        out.consts[i] = TapConst{c.firstRow, c.rows, c.stage, c.uses};
    }
    out.draws.resize(h.drawCount);
    for (uint32_t i = 0; i < h.drawCount; ++i) {
        VPModelTapDraw r;
        std::memcpy(&r, data + h.drawOffset + i * sizeof r, sizeof r);
        TapDraw& d = out.draws[i];
        if (!materialFromClass(r.cls, d.material)) {
            out = TapFrame();
            return Status::fail(Err::BadLayout, "draw " + std::to_string(i) + ": unknown class " + std::to_string(r.cls));
        }
        d.key = WireKey{r.key.ib, r.key.vb, r.key.t0, r.key.t1, r.key.startIndex, r.key.indexCount, r.key.baseVertex,
                        r.key.flags};
        d.bits = r.bits;
        d.flags = r.flags;
        d.target = r.target;
        d.instanceCount = r.instanceCount;
        d.startInstance = r.startInstance;
        d.instRow = r.instRow;
        d.instRows = r.instRows;
        d.vsCb1 = r.vsCb1;
        d.psCb1 = r.psCb1;
        d.order = r.order;
        d.skinId0 = r.skinId0;
        for (int k = 0; k < 3; ++k) d.env[k] = r.env[k];
    }
    Status s = checkFrame(out);
    if (!s.ok()) out = TapFrame();
    return s;
}

Status writeTap(const TapFrame& f, std::vector<uint8_t>& out) {
    out.clear();
    C5R_TRY(checkFrame(f));
    VPModelTapFrame h;
    std::memset(&h, 0, sizeof h);
    h.magic = VPSHELL_MODELTAP_MAGIC;
    h.version = VPSHELL_MODELTAP_VERSION;
    h.headerBytes = sizeof h;
    h.drawBytes = sizeof(VPModelTapDraw);
    h.frameSeq = f.frameSeq;
    h.presentCount = f.presentCount;
    h.drawCount = static_cast<uint32_t>(f.draws.size());
    h.rowCount = static_cast<uint32_t>(f.rows.size());
    h.constCount = static_cast<uint32_t>(f.consts.size());
    h.targetCount = static_cast<uint32_t>(f.targets.size());
    h.drawOffset = sizeof h;
    h.constOffset = align16(h.drawOffset + static_cast<uint64_t>(h.drawCount) * sizeof(VPModelTapDraw));
    h.targetOffset = align16(h.constOffset + static_cast<uint64_t>(h.constCount) * sizeof(VPModelTapConst));
    h.rowOffset = align16(h.targetOffset + static_cast<uint64_t>(h.targetCount) * sizeof(VPModelTapTarget));
    const uint64_t image = static_cast<uint64_t>(h.rowOffset) + static_cast<uint64_t>(h.rowCount) * 16u;
    if (image > 0xFFFFFFFFull) return Status::fail(Err::BadLayout, "frame image above 4 GB");
    h.imageBytes = static_cast<uint32_t>(image);
    h.setGeneration = f.setGeneration;
    h.latchedBits = f.latchedBits;
    h.wantBits = f.wantBits;
    h.dropped[0] = f.dropped[0];
    h.dropped[1] = f.dropped[1];
    h.passedTapped = f.passedTapped;
    h.truncated = f.truncated;
    out.assign(h.imageBytes, 0);
    for (size_t i = 0; i < f.draws.size(); ++i) {
        const TapDraw& d = f.draws[i];
        VPModelTapDraw r;
        std::memset(&r, 0, sizeof r);
        r.key.ib = d.key.ib;
        r.key.vb = d.key.vb;
        r.key.t0 = d.key.t0;
        r.key.t1 = d.key.t1;
        r.key.startIndex = d.key.startIndex;
        r.key.indexCount = d.key.indexCount;
        r.key.baseVertex = d.key.baseVertex;
        r.key.flags = d.key.flags;
        r.cls = classOf(d.material);
        r.bits = d.bits;
        r.flags = d.flags;
        r.target = d.target;
        r.instanceCount = d.instanceCount;
        r.startInstance = d.startInstance;
        r.instRow = d.instRow;
        r.instRows = d.instRows;
        r.vsCb1 = d.vsCb1;
        r.psCb1 = d.psCb1;
        r.order = d.order;
        r.skinId0 = d.skinId0;
        for (int k = 0; k < 3; ++k) r.env[k] = d.env[k];
        std::memcpy(out.data() + h.drawOffset + i * sizeof r, &r, sizeof r);
    }
    for (size_t i = 0; i < f.consts.size(); ++i) {
        VPModelTapConst c = {f.consts[i].firstRow, f.consts[i].rows, f.consts[i].stage, f.consts[i].uses};
        std::memcpy(out.data() + h.constOffset + i * sizeof c, &c, sizeof c);
    }
    for (size_t i = 0; i < f.targets.size(); ++i) {
        const TapTarget& t = f.targets[i];
        VPModelTapTarget r = {t.width, t.height, t.samples, t.format, t.depthWidth, t.depthHeight, t.draws, 0};
        std::memcpy(out.data() + h.targetOffset + i * sizeof r, &r, sizeof r);
    }
    if (h.rowCount) std::memcpy(out.data() + h.rowOffset, f.rows.data(), h.rowCount * 16ull);
    h.fnv1a64 = VPModelHash64(out.data() + h.headerBytes, h.imageBytes - h.headerBytes);
    std::memcpy(out.data(), &h, sizeof h);
    return Status::Ok();
}

Status readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    std::ifstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return Status::fail(Err::Io, "cannot open " + path);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (f.bad()) return Status::fail(Err::Io, "cannot read " + path);
    return Status::Ok();
}

Status writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return Status::fail(Err::Io, "cannot create " + path);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) return Status::fail(Err::Io, "cannot write " + path);
    return Status::Ok();
}

// ---------------------------------------------------------------------------------------------- the wire index
uint64_t WireIndex::textureId(const std::string& name) {
    if (name.empty()) return 0;
    const std::string key = c5m::basename_lower(name);
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    uint64_t id = 0;
    std::vector<uint8_t> b;
    std::string n = key;
    if (!files_->exists(n)) {
        const size_t dot = key.rfind('.');
        n = (dot == std::string::npos ? key : key.substr(0, dot)) + ".dds";
    }
    gamefs::DdsImage img;
    if (files_->exists(n) && files_->read(n, b).ok() && gamefs::parseDds(b.data(), b.size(), img).ok()) {
        std::vector<TextureLevel> mips;
        std::vector<std::vector<uint8_t>> swz;   // B8G8R8A8 as R8G8B8A8 (the EXE's upload)
        const bool bgra = img.format == DXGI_FORMAT_B8G8R8A8_UNORM;
        for (uint32_t m = 0; m < img.mipLevels && m < img.subresources.size(); ++m) {
            const gamefs::DdsSubresource& sr = img.subresources[m];
            const uint8_t* p = img.pixels() + sr.offset;
            if (bgra) {
                swz.emplace_back(p, p + sr.size);
                for (size_t i = 0; i + 3 < swz.back().size(); i += 4) std::swap(swz.back()[i], swz.back()[i + 2]);
                p = swz.back().data();
            }
            mips.push_back(TextureLevel{p, sr.size, sr.rowPitch});
        }
        id = textureIdentity(bgra ? DXGI_FORMAT_R8G8B8A8_UNORM : img.format, img.width, img.height, img.mipLevels,
                             mips);
    }
    if (!id) ++missingTextures_;
    textures_[key] = id;
    if (id) names_.emplace(id, key);   // the first name keeps it
    return id;
}

std::string WireIndex::textureName(uint64_t id) const {
    auto it = id ? names_.find(id) : names_.end();
    return it == names_.end() ? std::string() : it->second;
}

Status WireIndex::build() {
    const auto t0 = std::chrono::steady_clock::now();
    if (!lib_.loaded()) return Status::fail(Err::NotFound, "the model library is not loaded");
    keys_.assign(lib_.files().size(), {});
    sorted_.clear();
    geometry_.clear();
    failed_ = 0;
    for (uint32_t fi = 0; fi < lib_.files().size(); ++fi) {
        const ModelFile& mf = lib_.files()[fi];
        if (!mf.ok) continue;
        const ModelLibrary::Buffers* b = nullptr;
        if (!lib_.resident(fi, &b).ok() || !b) {
            ++failed_;
            continue;
        }
        uint64_t ib = 0;
        if (!wireIndexHash(b->indices.data(), b->indices.size(), mf.layout.indexBytes, ib).ok()) {
            ++failed_;
            continue;
        }
        const uint64_t vbRigid = b->vertices.empty() ? 0 : wireHash(b->vertices.data(), b->vertices.size());
        const uint64_t vbSkin =
            b->skinnedVertices.empty() ? 0 : wireHash(b->skinnedVertices.data(), b->skinnedVertices.size());
        std::vector<WireKey>& ks = keys_[fi];
        ks.resize(mf.layout.parts.size());
        for (uint32_t pi = 0; pi < mf.layout.parts.size(); ++pi) {
            const Part& p = mf.layout.parts[pi];
            WireKey& k = ks[pi];
            k.ib = ib;
            k.vb = p.skinned ? vbSkin : vbRigid;
            k.t0 = textureId(p.t0);
            k.t1 = textureId(p.t1);
            k.startIndex = p.key.firstIndex;
            k.indexCount = p.key.indexCount;
            k.baseVertex = p.key.baseVertex;
            k.flags = mf.layout.indexBytes == 4 ? kWireIndex32 : 0;
            sorted_.emplace_back(k, ModelLibrary::Hit{fi, pi});
            WireKey g = k;
            g.t0 = g.t1 = 0;
            geometry_.emplace_back(g, ModelLibrary::Hit{fi, pi});
        }
    }
    auto byKey = [](const std::pair<WireKey, ModelLibrary::Hit>& a, const std::pair<WireKey, ModelLibrary::Hit>& b) {
        if (a.first < b.first) return true;
        if (b.first < a.first) return false;
        return std::tie(a.second.file, a.second.part) < std::tie(b.second.file, b.second.part);
    };
    std::sort(sorted_.begin(), sorted_.end(), byKey);
    std::sort(geometry_.begin(), geometry_.end(), byKey);
    seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return Status::Ok();
}

namespace {
std::vector<ModelLibrary::Hit> range(const std::vector<std::pair<WireKey, ModelLibrary::Hit>>& v, const WireKey& k) {
    std::vector<ModelLibrary::Hit> out;
    auto lo = std::lower_bound(v.begin(), v.end(), k, [](const std::pair<WireKey, ModelLibrary::Hit>& a,
                                                          const WireKey& b) { return a.first < b; });
    for (; lo != v.end() && lo->first == k; ++lo) out.push_back(lo->second);
    return out;
}
}  // namespace

std::vector<ModelLibrary::Hit> WireIndex::find(const WireKey& k) const { return range(sorted_, k); }

namespace {
const WireIndex::Entry* rangeOf(const std::vector<WireIndex::Entry>& v, const WireKey& k, size_t& count) {
    auto lo = std::lower_bound(v.begin(), v.end(), k,
                               [](const WireIndex::Entry& a, const WireKey& b) { return a.first < b; });
    auto hi = lo;
    while (hi != v.end() && hi->first == k) ++hi;
    count = static_cast<size_t>(hi - lo);
    return count ? &*lo : nullptr;
}
}  // namespace

const WireIndex::Entry* WireIndex::findRange(const WireKey& k, size_t& count) const { return rangeOf(sorted_, k, count); }

bool WireIndex::hasGeometry(const WireKey& k) const {
    WireKey g = k;
    g.t0 = g.t1 = 0;
    size_t n = 0;
    rangeOf(geometry_, g, n);
    return n != 0;
}

std::vector<ModelLibrary::Hit> WireIndex::findGeometry(const WireKey& k) const {
    WireKey g = k;
    g.t0 = g.t1 = 0;
    return range(geometry_, g);
}

const WireKey* WireIndex::key(uint32_t file, uint32_t part) const {
    if (file >= keys_.size() || part >= keys_[file].size()) return nullptr;
    return &keys_[file][part];
}

// ---------------------------------------------------------------------------------------------- resolve
Status resolve(const WireIndex& index, const ModelLibrary& lib, const TapFrame& f, ModelScene& out,
               ResolveReport* report) {
    out = ModelScene();
    ResolveReport rep;
    C5R_TRY(checkFrame(f));
    out.frameSeq = f.frameSeq;
    out.targets = f.targets;
    out.rows = f.rows;
    out.consts = f.consts;
    out.items.reserve(f.draws.size());
    for (uint32_t i = 0; i < f.draws.size(); ++i) {
        const TapDraw& d = f.draws[i];
        ++rep.draws;
        // ticket 4884: the index's entries in place (was: two vectors per draw), the first that fits and their count
        size_t nHits = 0, nFit = 0;
        const WireIndex::Entry* hits = index.findRange(d.key, nHits);
        const ModelLibrary::Hit* first = nullptr;
        for (size_t k = 0; k < nHits; ++k) {
            const ModelLibrary::Hit& h = hits[k].second;
            const ModelFile& mf = lib.files()[h.file];
            if (formFits(d.material, mf.layout, mf.layout.parts[h.part])) {
                if (!first) first = &h;
                ++nFit;
            }
        }
        if (!nFit) {
            if (nHits)
                ++rep.materialMiss;
            else if (index.hasGeometry(d.key))
                ++rep.textureMiss;
            ++rep.unknown;
            ++out.unknownDraws;
            out.unknown.push_back(i);
            continue;
        }
        if (nFit > 1) {
            ++rep.ambiguous;
            ++out.ambiguousDraws;
        }
        DrawItem it;
        it.file = first->file;
        it.part = first->part;
        it.material = d.material;
        it.target = d.target;
        it.instanceCount = d.instanceCount;
        it.instRow = d.instRow;
        it.vsCb1 = d.vsCb1;
        it.psCb1 = d.psCb1;
        for (int k = 0; k < 3; ++k) it.env[k] = d.env[k];
        it.tapDraw = i;
        out.items.push_back(it);
        ++rep.resolved;
    }
    if (report) *report = rep;
    return Status::Ok();
}

// ---------------------------------------------------------------------------------------------- .c5models
namespace {

constexpr uint32_t kSceneMagic = 0x444D3543u;   // 'C5MD'
constexpr uint32_t kSceneVersion = 1;

#pragma pack(push, 4)
struct SceneHeader {   // 64 bytes
    uint32_t magic, version, headerBytes, itemBytes;
    uint64_t frameSeq;
    uint32_t itemCount, stringCount, stringBytes, rowCount, constCount, targetCount;
    uint32_t unknownDraws, ambiguousDraws, unknownCount, reserved;
};
struct SceneItem {   // 72 bytes
    uint32_t file;     // into the string table: the gr2 name
    float scale;
    uint32_t part;
    uint32_t material;
    uint32_t target, instanceCount, instRow, vsCb1, psCb1, tapDraw;
    uint64_t env[3];
    uint32_t reserved[2];
};
#pragma pack(pop)
static_assert(sizeof(SceneHeader) == 64, "layout");
static_assert(sizeof(SceneItem) == 72, "layout");

void put(std::vector<uint8_t>& o, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    o.insert(o.end(), b, b + n);
}

struct Cursor {
    const uint8_t* p;
    size_t left;
    bool take(void* dst, size_t n) {
        if (n > left) return false;
        if (n) std::memcpy(dst, p, n);
        p += n;
        left -= n;
        return true;
    }
};

}  // namespace

Status writeScene(const ModelScene& s, const ModelLibrary& lib, std::vector<uint8_t>& out) {
    out.clear();
    std::vector<std::string> names;
    std::map<std::string, uint32_t> nameIndex;
    std::vector<SceneItem> items;
    for (const DrawItem& it : s.items) {
        if (it.file >= lib.files().size() || it.part >= lib.files()[it.file].layout.parts.size())
            return Status::fail(Err::BadLayout, "an item's file or part is not in the library");
        const ModelFile& mf = lib.files()[it.file];
        auto ins = nameIndex.emplace(mf.gr2, static_cast<uint32_t>(names.size()));
        if (ins.second) names.push_back(mf.gr2);
        SceneItem r;
        std::memset(&r, 0, sizeof r);
        r.file = ins.first->second;
        r.scale = mf.scale;
        r.part = it.part;
        r.material = static_cast<uint32_t>(it.material);
        r.target = it.target;
        r.instanceCount = it.instanceCount;
        r.instRow = it.instRow;
        r.vsCb1 = it.vsCb1;
        r.psCb1 = it.psCb1;
        r.tapDraw = it.tapDraw;
        for (int k = 0; k < 3; ++k) r.env[k] = it.env[k];
        items.push_back(r);
    }
    std::vector<uint8_t> strings;
    for (const std::string& n : names) {
        const uint32_t len = static_cast<uint32_t>(n.size());
        put(strings, &len, 4);
        put(strings, n.data(), n.size());
    }
    SceneHeader h;
    std::memset(&h, 0, sizeof h);
    h.magic = kSceneMagic;
    h.version = kSceneVersion;
    h.headerBytes = sizeof h;
    h.itemBytes = sizeof(SceneItem);
    h.frameSeq = s.frameSeq;
    h.itemCount = static_cast<uint32_t>(items.size());
    h.stringCount = static_cast<uint32_t>(names.size());
    h.stringBytes = static_cast<uint32_t>(strings.size());
    h.rowCount = static_cast<uint32_t>(s.rows.size());
    h.constCount = static_cast<uint32_t>(s.consts.size());
    h.targetCount = static_cast<uint32_t>(s.targets.size());
    h.unknownDraws = s.unknownDraws;
    h.ambiguousDraws = s.ambiguousDraws;
    h.unknownCount = static_cast<uint32_t>(s.unknown.size());
    put(out, &h, sizeof h);
    put(out, strings.data(), strings.size());
    if (!items.empty()) put(out, items.data(), items.size() * sizeof(SceneItem));
    if (!s.rows.empty()) put(out, s.rows.data(), s.rows.size() * sizeof(Float4));
    for (const TapConst& c : s.consts) put(out, &c, sizeof c);
    for (const TapTarget& t : s.targets) put(out, &t, sizeof t);
    if (!s.unknown.empty()) put(out, s.unknown.data(), s.unknown.size() * 4);
    return Status::Ok();
}

Status readScene(const uint8_t* data, size_t size, const ModelLibrary& lib, ModelScene& out) {
    out = ModelScene();
    if (!data || size < 4) return Status::fail(Err::Truncated, "shorter than the scene's magic");
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    if (magic != kSceneMagic) return Status::fail(Err::BadMagic, "not a .c5models scene (magic)");
    Cursor c{data, size};
    SceneHeader h;
    if (!c.take(&h, sizeof h)) return Status::fail(Err::Truncated, "shorter than the scene header");
    if (h.version != kSceneVersion) return Status::fail(Err::BadVersion, "scene version " + std::to_string(h.version));
    if (h.headerBytes != sizeof h || h.itemBytes != sizeof(SceneItem))
        return Status::fail(Err::BadLayout, "header or item size not this layout's");
    const uint64_t need = static_cast<uint64_t>(h.stringBytes) + static_cast<uint64_t>(h.itemCount) * sizeof(SceneItem) +
                          static_cast<uint64_t>(h.rowCount) * sizeof(Float4) +
                          static_cast<uint64_t>(h.constCount) * sizeof(TapConst) +
                          static_cast<uint64_t>(h.targetCount) * sizeof(TapTarget) + h.unknownCount * 4ull;
    if (need > c.left) return Status::fail(Err::Truncated, "the scene's arrays end past the data");
    if (h.stringCount > h.stringBytes / 4u) return Status::fail(Err::BadLayout, "more strings than the table can hold");
    std::vector<std::string> names;
    Cursor sc{c.p, h.stringBytes};
    for (uint32_t i = 0; i < h.stringCount; ++i) {
        uint32_t len = 0;
        if (!sc.take(&len, 4) || len > sc.left) return Status::fail(Err::BadLayout, "a string past the string table");
        names.emplace_back(reinterpret_cast<const char*>(sc.p), len);
        sc.p += len;
        sc.left -= len;
    }
    if (sc.left) return Status::fail(Err::BadLayout, "bytes left over in the string table");
    c.p += h.stringBytes;
    c.left -= h.stringBytes;
    out.frameSeq = h.frameSeq;
    out.unknownDraws = h.unknownDraws;
    out.ambiguousDraws = h.ambiguousDraws;
    std::vector<SceneItem> items(h.itemCount);
    if (h.itemCount) c.take(items.data(), items.size() * sizeof(SceneItem));
    out.rows.resize(h.rowCount);
    if (h.rowCount) c.take(out.rows.data(), out.rows.size() * sizeof(Float4));
    out.consts.resize(h.constCount);
    if (h.constCount) c.take(out.consts.data(), out.consts.size() * sizeof(TapConst));
    out.targets.resize(h.targetCount);
    if (h.targetCount) c.take(out.targets.data(), out.targets.size() * sizeof(TapTarget));
    out.unknown.resize(h.unknownCount);
    if (h.unknownCount) c.take(out.unknown.data(), out.unknown.size() * 4);
    for (const TapConst& k : out.consts)
        if (static_cast<uint64_t>(k.firstRow) + k.rows > out.rows.size()) {
            out = ModelScene();
            return Status::fail(Err::BadLayout, "a const outside the rows");
        }
    for (uint32_t i = 0; i < items.size(); ++i) {
        const SceneItem& r = items[i];
        const std::string who = "item " + std::to_string(i);
        Status bad;
        if (r.file >= names.size()) bad = Status::fail(Err::BadLayout, who + ": file name index out of range");
        else if (r.material < 3 || r.material > 6) bad = Status::fail(Err::BadLayout, who + ": unknown material");
        else if (r.target >= out.targets.size()) bad = Status::fail(Err::BadLayout, who + ": target out of range");
        else if ((r.vsCb1 != kNoConst && r.vsCb1 >= out.consts.size()) ||
                 (r.psCb1 != kNoConst && r.psCb1 >= out.consts.size()))
            bad = Status::fail(Err::BadLayout, who + ": constant reference out of range");
        else if (r.instanceCount == 0 ||
                 static_cast<uint64_t>(r.instRow) +
                         static_cast<uint64_t>(rowsPerInstance(static_cast<Material>(r.material))) * r.instanceCount >
                     out.rows.size())
            bad = Status::fail(Err::BadLayout, who + ": instance rows outside the rows");
        if (!bad.ok()) {
            out = ModelScene();
            return bad;
        }
        const int32_t fi = lib.fileIndex(names[r.file], r.scale);
        if (fi < 0) {
            out = ModelScene();
            return Status::fail(Err::NotFound, who + ": " + names[r.file] + " not in the library");
        }
        if (r.part >= lib.files()[fi].layout.parts.size()) {
            out = ModelScene();
            return Status::fail(Err::BadLayout, who + ": part out of range for " + names[r.file]);
        }
        DrawItem it;
        it.file = static_cast<uint32_t>(fi);
        it.part = r.part;
        it.material = static_cast<Material>(r.material);
        it.target = r.target;
        it.instanceCount = r.instanceCount;
        it.instRow = r.instRow;
        it.vsCb1 = r.vsCb1;
        it.psCb1 = r.psCb1;
        it.tapDraw = r.tapDraw;
        for (int k = 0; k < 3; ++k) it.env[k] = r.env[k];
        out.items.push_back(it);
    }
    return Status::Ok();
}

}}  // namespace c5r::landmarks
