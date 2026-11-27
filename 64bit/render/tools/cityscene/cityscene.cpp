// cityscene - the offline harness of the BuildingShader draw layer (ticket 4873, epic 0561; research/05-graphics-
// composition/landmark-renderer.md 7). The Python driver render/tools/cityscene/cityscene.py writes a frame list from
// a 3907/3910 capture export and runs this tool; game-derived inputs are read at run time from the export and the
// install, outputs go to the work folders (D83: nothing of the game is committed).
//
//   cityscene tap FRAME.tsv OUT.mtap
//       The tap frame of the listed draws, made from the capture's own buffers as the compositor taps them (4874's
//       frame image, shell_model_tap.h): per draw the wire key (index buffer from index 1, the whole slot-0 buffer, the
//       t0 / t1 identity, the range), the instance rows (slot 1's four rows per instance; material 3's VS cb0 rows 0-3;
//       material 6's 96 palette rows of VS t27 per slot-1 id), VS and PS cb1 (deduplicated), the PS t2-t4 identity and
//       the target. Written, read back with the module's reader and compared field by field. Exit 0 written, 1 an
//       input unreadable or the round trip unequal, 2 usage.
//   cityscene run --game INSTALL --db DB --exe EXE --frame FRAME.tsv --tap IN.mtap --out DIR [--target N]
//       The library loaded from the install, its wire index built, the tap resolved into a scene (written as
//       DIR/scene.c5models, read back and compared), the scene drawn with the EXE's programs into a target of the
//       capture's size per target (DIR/ours_<n>.rgba, DIR/ours_<n>_id.u32: 1 + the item index, 0 none), the PS
//       t2-t9 from the capture's textures named in FRAME.tsv. DIR/items.tsv lists the items (tap draw, file, part),
//       DIR/unknown.tsv the draws no part holds with their geometry candidates. Exit 0 drawn, 1 a step failed, 2 usage.
//       --inputs capture|install|none (ticket 4882; default capture): where PS t2-t6 and t9 come from. install = the
//       server's sources (c5r/landmarks/inputs.h: the install's cubes by identity, fogramp.dds, the Buildings and
//       TerrainFOW ColorKey volumes), t7 / t8 still the capture's (the server's own fog stands in for them); the
//       uploaded bytes are compared with the capture's t5 / t6 / t9 files by SHA-256 ("inputs t5 ... equal 1"). none =
//       t2-t9 unbound, as 4875's server drew them live (s3 point 8: solid black).
//       --bench FRAMES (ticket 4884): after the pictures, the server's per-frame model path repeated FRAMES times with
//       stage timers (runBench below); DIR/bench.tsv holds a row per frame, the "bench" lines their statistics.
//
// FRAME.tsv (tab-separated, "-" = none):
//   target IDX W H SAMPLES FORMAT DSV_W DSV_H
//   tex ID FORMAT VIEW_FORMAT DIM(2|3|4=cube) W H D MIPS ARRAY PATH SIZES(comma-separated subresource bytes)
//   draw IDX N MAT TARGET INDEX_COUNT START_INDEX BASE_VERTEX INSTANCES START_INSTANCE IB_BYTES IB IB_OFF VB VB_STRIDE
//        VB_OFF S1 S1_STRIDE S1_OFF CB0 VS_CB1 PS_CB1 T27 T0 T1 T2 T3 T4 T5 T6 T7 T8 T9
#include <d3d11.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5r/cities/catalogue.h"
#include "c5r/gamefs/dds.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/landmarks/draw.h"
#include "c5r/landmarks/inputs.h"
#include "c5r/landmarks/library.h"
#include "c5r/landmarks/tap.h"
#include "c5r/units/art.h"    // units::GameFilesProvider
#include "c5r/units/draw.h"   // units::loadStringDatabases
#include "shell_model_tap.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

int usage(const char* why) {
    std::fprintf(stderr,
                 "cityscene: %s\nusage: cityscene tap FRAME.tsv OUT.mtap\n"
                 "       cityscene run --game INSTALL --db DB --exe EXE --frame FRAME.tsv --tap IN.mtap --out DIR "
                 "[--target N] [--inputs capture|install|none] [--bench FRAMES]\n",
                 why);
    return 2;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

struct TexDesc {
    std::string id, path;
    uint32_t format = 0, viewFormat = 0, dim = 2, w = 1, h = 1, d = 1, mips = 1, array = 1;
    std::vector<size_t> sizes;
};

struct DrawDesc {
    uint32_t idx = 0, n = 0, mat = 0, target = 0, indexCount = 0, startIndex = 0, instances = 1, startInstance = 0;
    int32_t baseVertex = 0;
    uint32_t ibBytes = 2, ibOff = 0, vbStride = 0, vbOff = 0, s1Stride = 0, s1Off = 0;
    std::string ib, vb, s1, cb0, vsCb1, psCb1, t27;
    std::string tex[10];
};

struct FrameList {
    std::vector<TapTarget> targets;
    std::map<std::string, TexDesc> textures;
    std::vector<DrawDesc> draws;
};

bool parseFrame(const std::string& path, FrameList& out, std::string& why) {
    std::ifstream f(path);
    if (!f) {
        why = "cannot open " + path;
        return false;
    }
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> c = split(line, '\t');
        auto u = [&](size_t i) { return static_cast<uint32_t>(std::stoul(c.at(i))); };
        try {
            if (c[0] == "target") {
                TapTarget t;
                const uint32_t idx = u(1);
                t.width = u(2);
                t.height = u(3);
                t.samples = u(4);
                t.format = u(5);
                t.depthWidth = u(6);
                t.depthHeight = u(7);
                if (idx != out.targets.size()) throw std::out_of_range("targets out of order");
                out.targets.push_back(t);
            } else if (c[0] == "tex") {
                TexDesc t;
                t.id = c.at(1);
                t.format = u(2);
                t.viewFormat = u(3);
                t.dim = u(4);
                t.w = u(5);
                t.h = u(6);
                t.d = u(7);
                t.mips = u(8);
                t.array = u(9);
                t.path = c.at(10);
                for (const std::string& s : split(c.at(11), ',')) t.sizes.push_back(std::stoull(s));
                out.textures[t.id] = t;
            } else if (c[0] == "draw") {
                if (c.size() < 33) throw std::out_of_range("draw line short");
                DrawDesc d;
                d.idx = u(1);
                d.n = u(2);
                d.mat = u(3);
                d.target = u(4);
                d.indexCount = u(5);
                d.startIndex = u(6);
                d.baseVertex = std::stoi(c[7]);
                d.instances = u(8);
                d.startInstance = u(9);
                d.ibBytes = u(10);
                d.ib = c[11];
                d.ibOff = u(12);
                d.vb = c[13];
                d.vbStride = u(14);
                d.vbOff = u(15);
                d.s1 = c[16];
                d.s1Stride = c[17] == "-" ? 0 : u(17);
                d.s1Off = c[18] == "-" ? 0 : u(18);
                d.cb0 = c[19];
                d.vsCb1 = c[20];
                d.psCb1 = c[21];
                d.t27 = c[22];
                for (int k = 0; k < 10; ++k) d.tex[k] = c[23 + k];
                if (d.idx != out.draws.size()) throw std::out_of_range("draws out of order");
                out.draws.push_back(d);
            }
        } catch (const std::exception& e) {
            why = path + " line " + std::to_string(ln) + ": " + e.what();
            return false;
        }
    }
    return true;
}

// Files read once (the export lives on F:, at its I/O limit: every buffer and texture once per run).
struct FileCache {
    std::map<std::string, std::vector<uint8_t>> files;
    const std::vector<uint8_t>* get(const std::string& p) {
        if (p.empty() || p == "-") return nullptr;
        auto it = files.find(p);
        if (it != files.end()) return &it->second;
        std::vector<uint8_t> b;
        if (!readFileBytes(p, b).ok()) return nullptr;
        return &(files[p] = std::move(b));
    }
};

// Row pitch of subresource k of a texture (sizes as the export stores them: pitch x rows x depth, tight here).
uint32_t rowsOf(uint32_t format, uint32_t h) {
    uint32_t bw = 1;
    VPModelFormatBlock(format, &bw);
    return (std::max(1u, h) + bw - 1) / bw;
}

uint64_t texIdentity(const TexDesc& t, FileCache& fc) {
    const std::vector<uint8_t>* b = fc.get(t.path);
    if (!b) return 0;
    std::vector<TextureLevel> mips;
    size_t off = 0;
    for (uint32_t m = 0; m < t.mips && m < t.sizes.size(); ++m) {
        const uint32_t mh = std::max(1u, t.h >> m), md = std::max(1u, t.d >> m);
        const size_t sz = t.sizes[m];
        if (off + sz > b->size()) break;
        const uint32_t pitch = static_cast<uint32_t>(sz / (static_cast<size_t>(rowsOf(t.format, mh)) * md));
        mips.push_back(TextureLevel{b->data() + off, sz, pitch});
        off += sz;
    }
    return textureIdentity(t.format, t.w, t.h, t.mips, mips);
}

Status buildTap(const FrameList& fl, FileCache& fc, TapFrame& tf, std::string& notes) {
    tf = TapFrame();
    tf.frameSeq = 1;
    tf.targets = fl.targets;
    std::map<std::vector<uint8_t>, uint32_t> constIndex;
    auto addConst = [&](const std::string& path, uint32_t stage) -> uint32_t {
        const std::vector<uint8_t>* b = fc.get(path);
        if (!b || b->empty() || b->size() % 16 || b->size() > VPSHELL_MODELTAP_CB_MAX_BYTES) return kNoConst;
        auto it = constIndex.find(*b);
        if (it != constIndex.end()) {
            ++tf.consts[it->second].uses;
            return it->second;
        }
        TapConst c;
        c.firstRow = static_cast<uint32_t>(tf.rows.size());
        c.rows = static_cast<uint32_t>(b->size() / 16);
        c.stage = stage;
        c.uses = 1;
        const size_t r0 = tf.rows.size();
        tf.rows.resize(r0 + c.rows);
        std::memcpy(&tf.rows[r0], b->data(), b->size());
        const uint32_t idx = static_cast<uint32_t>(tf.consts.size());
        tf.consts.push_back(c);
        constIndex[*b] = idx;
        return idx;
    };
    for (const DrawDesc& d : fl.draws) {
        const std::string who = "draw " + std::to_string(d.n);
        TapDraw t;
        if (d.mat < 3 || d.mat > 6) return Status::fail(Err::BadLayout, who + ": material " + std::to_string(d.mat));
        t.material = static_cast<Material>(d.mat);
        t.target = d.target;
        t.instanceCount = d.instances;
        t.startInstance = d.startInstance;
        t.order = d.idx;
        t.flags = VPMTAP_DRAW_DROPPED;
        const std::vector<uint8_t>* ib = fc.get(d.ib);
        const std::vector<uint8_t>* vb = fc.get(d.vb);
        if (!ib || !vb) return Status::fail(Err::Io, who + ": index or vertex buffer unreadable");
        if (d.ibOff != 0 || d.vbOff != 0) notes += who + ": IB or VB offset not 0\n";
        C5R_TRY(wireIndexHash(ib->data(), ib->size(), d.ibBytes, t.key.ib));
        t.key.vb = wireHash(vb->data(), vb->size());
        t.key.flags = d.ibBytes == 4 ? kWireIndex32 : 0;
        t.key.startIndex = d.startIndex;
        t.key.indexCount = d.indexCount;
        t.key.baseVertex = d.baseVertex;
        auto texId = [&](const std::string& id) -> uint64_t {
            auto it = fl.textures.find(id);
            return it == fl.textures.end() ? 0 : texIdentity(it->second, fc);
        };
        t.key.t0 = texId(d.tex[0]);
        t.key.t1 = texId(d.tex[1]);
        for (int k = 0; k < 3; ++k) t.env[k] = texId(d.tex[2 + k]);
        t.instRow = static_cast<uint32_t>(tf.rows.size());
        t.instRows = rowsPerInstance(t.material);
        const size_t r0 = tf.rows.size();
        tf.rows.resize(r0 + static_cast<size_t>(t.instRows) * t.instanceCount);
        Float4* rows = &tf.rows[r0];
        if (t.material == Material::Single) {
            const std::vector<uint8_t>* cb0 = fc.get(d.cb0);
            if (!cb0 || cb0->size() < 64) return Status::fail(Err::Io, who + ": VS cb0 missing or short");
            std::memcpy(rows, cb0->data(), 64);
        } else {
            const std::vector<uint8_t>* s1 = fc.get(d.s1);
            if (!s1) return Status::fail(Err::Io, who + ": slot 1 unreadable");
            const size_t stride = t.material == Material::Skinned ? 4 : 64;
            if (d.s1Stride != stride) return Status::fail(Err::BadLayout, who + ": slot 1 stride not " + std::to_string(stride));
            const size_t first = d.s1Off + static_cast<size_t>(d.startInstance) * stride;
            if (first + stride * d.instances > s1->size()) return Status::fail(Err::Truncated, who + ": slot 1 short");
            if (t.material == Material::Skinned) {
                const std::vector<uint8_t>* pal = fc.get(d.t27);
                if (!pal) return Status::fail(Err::Io, who + ": VS t27 unreadable");
                for (uint32_t i = 0; i < d.instances; ++i) {
                    uint32_t id;
                    std::memcpy(&id, s1->data() + first + i * 4, 4);
                    if (i == 0) t.skinId0 = id;
                    const size_t off = static_cast<size_t>(id) * VPSHELL_MODELTAP_PALETTE_ROWS * 16;
                    if (off + VPSHELL_MODELTAP_PALETTE_ROWS * 16 > pal->size())
                        return Status::fail(Err::Truncated, who + ": palette rows past t27");
                    std::memcpy(rows + static_cast<size_t>(i) * VPSHELL_MODELTAP_PALETTE_ROWS, pal->data() + off,
                                VPSHELL_MODELTAP_PALETTE_ROWS * 16);
                }
            } else {
                std::memcpy(rows, s1->data() + first, 64ull * d.instances);
            }
        }
        t.vsCb1 = addConst(d.vsCb1, 1);
        t.psCb1 = addConst(d.psCb1, 2);
        if (d.target < tf.targets.size()) ++tf.targets[d.target].draws;
        tf.draws.push_back(t);
    }
    tf.dropped[0] = tf.dropped[1] = 0;
    for (const TapDraw& t : tf.draws) {
        if (t.material == Material::Blocks || t.material == Material::Single) ++tf.dropped[0];
        else if (t.material == Material::Instanced) ++tf.dropped[1];
    }
    tf.latchedBits = tf.wantBits = 6;
    return Status::Ok();
}

bool sameFrame(const TapFrame& a, const TapFrame& b, std::string& why) {
    if (a.draws.size() != b.draws.size() || a.rows.size() != b.rows.size() || a.consts.size() != b.consts.size() ||
        a.targets.size() != b.targets.size()) {
        why = "counts differ";
        return false;
    }
    if (!a.rows.empty() && std::memcmp(a.rows.data(), b.rows.data(), a.rows.size() * sizeof(Float4))) {
        why = "rows differ";
        return false;
    }
    for (size_t i = 0; i < a.draws.size(); ++i) {
        const TapDraw &x = a.draws[i], &y = b.draws[i];
        if (x.key != y.key || x.material != y.material || x.target != y.target || x.instanceCount != y.instanceCount ||
            x.instRow != y.instRow || x.instRows != y.instRows || x.vsCb1 != y.vsCb1 || x.psCb1 != y.psCb1 ||
            x.env[0] != y.env[0] || x.env[1] != y.env[1] || x.env[2] != y.env[2] || x.flags != y.flags ||
            x.order != y.order || x.skinId0 != y.skinId0) {
            why = "draw " + std::to_string(i) + " differs";
            return false;
        }
    }
    return true;
}

int cmdTap(int argc, char** argv) {
    if (argc != 4) return usage("tap takes FRAME.tsv OUT.mtap");
    FrameList fl;
    std::string why;
    if (!parseFrame(argv[2], fl, why)) {
        std::fprintf(stderr, "cityscene tap: %s\n", why.c_str());
        return 1;
    }
    FileCache fc;
    TapFrame tf;
    std::string notes;
    Status s = buildTap(fl, fc, tf, notes);
    if (!s.ok()) {
        std::fprintf(stderr, "cityscene tap: %s\n", s.str().c_str());
        return 1;
    }
    std::vector<uint8_t> bytes;
    s = writeTap(tf, bytes);
    if (s.ok()) s = writeFileBytes(argv[3], bytes);
    TapFrame back;
    if (s.ok()) s = readTap(bytes.data(), bytes.size(), back);
    if (!s.ok()) {
        std::fprintf(stderr, "cityscene tap: %s\n", s.str().c_str());
        return 1;
    }
    if (!sameFrame(tf, back, why)) {
        std::fprintf(stderr, "cityscene tap: round trip: %s\n", why.c_str());
        return 1;
    }
    size_t mb = 0;
    for (auto& kv : fc.files) mb += kv.second.size();
    std::printf("tap %s draws %zu rows %zu consts %zu targets %zu bytes %zu (read %zu files, %.1f MB)\n%s", argv[3],
                tf.draws.size(), tf.rows.size(), tf.consts.size(), tf.targets.size(), bytes.size(), fc.files.size(),
                mb / 1048576.0, notes.c_str());
    return 0;
}

// ---------------------------------------------------------------------------------------------- run
struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ~Gpu() {
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }
};

template <class T> void rel(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

Status createExportTexture(ID3D11Device* dev, const TexDesc& t, FileCache& fc, ID3D11Resource** res,
                           ID3D11ShaderResourceView** srv) {
    *res = nullptr;
    *srv = nullptr;
    const std::vector<uint8_t>* b = fc.get(t.path);
    if (!b) return Status::fail(Err::Io, "texture " + t.path);
    const uint32_t slices = t.dim == 3 ? 1 : t.array;
    if (t.sizes.size() != static_cast<size_t>(slices) * t.mips) return Status::fail(Err::BadLayout, "subresource count of " + t.id);
    std::vector<D3D11_SUBRESOURCE_DATA> init(t.sizes.size());
    size_t off = 0;
    for (uint32_t a = 0; a < slices; ++a)
        for (uint32_t m = 0; m < t.mips; ++m) {
            const size_t k = static_cast<size_t>(a) * t.mips + m;
            const uint32_t mh = std::max(1u, t.h >> m), md = std::max(1u, t.d >> m);
            if (off + t.sizes[k] > b->size()) return Status::fail(Err::Truncated, "texture " + t.id);
            init[k].pSysMem = b->data() + off;
            init[k].SysMemPitch = static_cast<UINT>(t.sizes[k] / (static_cast<size_t>(rowsOf(t.format, mh)) * md));
            init[k].SysMemSlicePitch = static_cast<UINT>(t.sizes[k] / md);
            off += t.sizes[k];
        }
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = static_cast<DXGI_FORMAT>(t.viewFormat);
    HRESULT hr;
    if (t.dim == 3) {
        D3D11_TEXTURE3D_DESC d = {};
        d.Width = t.w;
        d.Height = t.h;
        d.Depth = t.d;
        d.MipLevels = t.mips;
        d.Format = static_cast<DXGI_FORMAT>(t.format);
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture3D* tex = nullptr;
        hr = dev->CreateTexture3D(&d, init.data(), &tex);
        *res = tex;
        vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        vd.Texture3D.MipLevels = t.mips;
    } else {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = t.w;
        d.Height = t.h;
        d.MipLevels = t.mips;
        d.ArraySize = t.array;
        d.Format = static_cast<DXGI_FORMAT>(t.format);
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = t.dim == 4 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        ID3D11Texture2D* tex = nullptr;
        hr = dev->CreateTexture2D(&d, init.data(), &tex);
        *res = tex;
        if (t.dim == 4) {
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            vd.TextureCube.MipLevels = t.mips;
        } else if (t.array > 1) {
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            vd.Texture2DArray.MipLevels = t.mips;
            vd.Texture2DArray.ArraySize = t.array;
        } else {
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = t.mips;
        }
    }
    if (FAILED(hr)) return Status::fail(Err::Device, "creating texture " + t.id + ": " + std::to_string(hr));
    if (FAILED(dev->CreateShaderResourceView(*res, &vd, srv))) return Status::fail(Err::Device, "SRV of " + t.id);
    return Status::Ok();
}

Status readBack(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, uint32_t bpp,
                std::vector<uint8_t>& out) {
    D3D11_TEXTURE2D_DESC d;
    src->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &st))) return Status::fail(Err::Device, "staging texture");
    ctx->CopyResource(st, src);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) {
        st->Release();
        return Status::fail(Err::Device, "Map of the staging texture");
    }
    out.resize(static_cast<size_t>(d.Width) * d.Height * bpp);
    for (uint32_t y = 0; y < d.Height; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * d.Width * bpp, static_cast<const uint8_t*>(ms.pData) + y * ms.RowPitch,
                    static_cast<size_t>(d.Width) * bpp);
    ctx->Unmap(st, 0);
    st->Release();
    return Status::Ok();
}

std::string hex64(uint64_t v) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

int fail(const char* what, const Status& s) {
    std::fprintf(stderr, "cityscene run: %s: %s\n", what, s.str().c_str());
    return 1;
}

// ---------------------------------------------------------------------------------------------- --bench (ticket 4884)
// The server's per-frame model path on the recorded tap, repeated: the reader's copy of the frame and the poll's
// compare with the previous one, readTap (every check and the hash), resolve (and resolveCpu), the layer's DROPPED
// filter and keepTapCamera (C5rLayer::accept), then per target as C5rLayer::draw does it: the PS inputs
// (InstallInputs::fill with --inputs install), applyFrameCamera at the tap's own camera rows (the server's frame
// camera; the picture is not read), ModelRenderer::draw without the id pass, with a GPU timestamp pair around it. Each
// frame is flushed and waited for (the GPU idle at each frame's start). The first 10 frames are not counted.
namespace {
double qpcMsNow() {
    static const double f = [] {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        return static_cast<double>(q.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return static_cast<double>(q.QuadPart) / f;
}

struct Stat {
    double mean = 0, sd = 0, median = 0, p95 = 0, mn = 0, mx = 0, ci95 = 0;
};
Stat statOf(std::vector<double> v) {
    Stat s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    s.mean = sum / v.size();
    double ss = 0;
    for (double x : v) ss += (x - s.mean) * (x - s.mean);
    s.sd = v.size() > 1 ? std::sqrt(ss / (v.size() - 1)) : 0;
    s.median = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
    s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(std::ceil(0.95 * v.size())) - 1)];
    s.mn = v.front();
    s.mx = v.back();
    s.ci95 = 1.96 * s.sd / std::sqrt(static_cast<double>(v.size()));
    return s;
}
}  // namespace

int runBench(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::vector<uint8_t>& tapBytes, const WireIndex& index,
             const ModelLibrary& lib, const CpuMeshStore* cpu, ModelRenderer& mr, InstallInputs* ii,
             const ModelScene& scene0, const std::vector<EnvInput>& envs, const std::vector<bool>& drawnTarget, int frames,
             const std::string& tsvPath) {
    const uint32_t nt = static_cast<uint32_t>(scene0.targets.size());
    struct Tgt {
        ID3D11Texture2D *rt = nullptr, *dt = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11Query *ts0 = nullptr, *ts1 = nullptr;
    };
    std::vector<Tgt> tg(nt);
    ID3D11Query* dj = nullptr;
    auto cleanup = [&]() {
        for (Tgt& t : tg) {
            rel(t.rtv);
            rel(t.dsv);
            rel(t.rt);
            rel(t.dt);
            rel(t.ts0);
            rel(t.ts1);
        }
        rel(dj);
    };
    D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    bool ok = SUCCEEDED(dev->CreateQuery(&qd, &dj));
    qd.Query = D3D11_QUERY_TIMESTAMP;
    for (uint32_t ti = 0; ok && ti < nt; ++ti) {
        if (!drawnTarget[ti]) continue;
        const TapTarget& tt = scene0.targets[ti];
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = tt.width;
        td.Height = tt.height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tg[ti].rt)) &&
             SUCCEEDED(dev->CreateRenderTargetView(tg[ti].rt, nullptr, &tg[ti].rtv));
        td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ok = ok && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tg[ti].dt)) &&
             SUCCEEDED(dev->CreateDepthStencilView(tg[ti].dt, nullptr, &tg[ti].dsv)) &&
             SUCCEEDED(dev->CreateQuery(&qd, &tg[ti].ts0)) && SUCCEEDED(dev->CreateQuery(&qd, &tg[ti].ts1));
    }
    if (!ok) {
        cleanup();
        return fail("bench", Status::fail(Err::Device, "creating the bench targets or queries"));
    }
    // columns: frame-level stages, then per target inputs, camera, upload, items, draw, gpu, calls, drawn
    std::vector<std::string> cols = {"copy_ms", "read_ms", "resolve_ms", "accept_rest_ms"};
    for (uint32_t ti = 0; ti < nt; ++ti) {
        if (!drawnTarget[ti]) continue;
        for (const char* c : {"inputs_ms", "camera_ms", "upload_ms", "items_ms", "draw_ms", "gpu_ms", "calls", "drawn"})
            cols.push_back("t" + std::to_string(ti) + "_" + c);
    }
    cols.push_back("cpu_total_ms");
    cols.push_back("gpu_total_ms");
    std::vector<std::vector<double>> rows;
    const int warm = 10;
    std::vector<uint8_t> prev;
    TapCameraRows tapRows;
    for (int f = 0; f < warm + frames; ++f) {
        std::vector<double> r;
        r.reserve(cols.size());
        ctx->Begin(dj);
        const double q0 = qpcMsNow();
        // the reader's copy (copy-then-check) and the poll's compare with the frame before (a still camera repeats it)
        std::vector<uint8_t> img(tapBytes.begin(), tapBytes.end());
        volatile bool same = prev.size() == img.size() && std::memcmp(prev.data(), img.data(), img.size()) == 0;
        (void)same;
        const double q1 = qpcMsNow();
        TapFrame tf;
        Status s = readTap(img.data(), img.size(), tf);
        const double q2 = qpcMsNow();
        ModelScene sc;
        if (s.ok()) s = resolve(index, lib, tf, sc, nullptr);
        if (s.ok() && cpu) s = resolveCpu(*cpu, index, tf, sc, nullptr);
        const double q3 = qpcMsNow();
        if (!s.ok()) {
            ctx->End(dj);
            cleanup();
            return fail("bench", s);
        }
        {   // C5rLayer::accept: the records the compositor dropped (the harness's tap flags every draw DROPPED)
            std::vector<DrawItem> keep;
            keep.reserve(sc.items.size());
            for (const DrawItem& it : sc.items)
                if (it.tapDraw < tf.draws.size() && (tf.draws[it.tapDraw].flags & VPMTAP_DRAW_DROPPED)) keep.push_back(it);
            sc.items.swap(keep);
            std::vector<CpuDrawItem> keepCpu;
            for (const CpuDrawItem& it : sc.cpuItems)
                if (it.tapDraw < tf.draws.size() && (tf.draws[it.tapDraw].flags & VPMTAP_DRAW_DROPPED)) keepCpu.push_back(it);
            sc.cpuItems.swap(keepCpu);
            keepTapCamera(sc, tapRows);
        }
        const double q4 = qpcMsNow();
        r.push_back(q1 - q0);
        r.push_back(q2 - q1);
        r.push_back(q3 - q2);
        r.push_back(q4 - q3);
        for (uint32_t ti = 0; ti < nt; ++ti) {
            if (!drawnTarget[ti]) continue;
            const double a0 = qpcMsNow();
            EnvInput env;
            if (ii) {   // the server's sources: t2-t4, t5, t6, t9 found per pass; t7 / t8 as the main run's
                env.frame[2] = envs[ti].frame[2];
                env.frame[3] = envs[ti].frame[3];
                ii->fill(sc, ti, env);
            } else {
                env = envs[ti];
            }
            const double a1 = qpcMsNow();
            // the frame's camera: the tap's own rows 0-3 (and 8-12) of the target's first VS cb1
            float vp[16], ov[16], fa[4];
            bool haveCam = false, haveFog = false;
            for (const DrawItem& it : sc.items) {
                if (it.target != ti || it.vsCb1 == kNoConst) continue;
                auto k = tapRows.find(it.vsCb1);
                if (k == tapRows.end()) continue;
                std::memcpy(vp, k->second.data(), sizeof vp);
                haveCam = true;
                const TapConst& c = sc.consts[it.vsCb1];
                if (c.rows >= 13) {
                    std::memcpy(ov, &sc.rows[c.firstRow + 8], sizeof ov);
                    std::memcpy(fa, &sc.rows[c.firstRow + 12], sizeof fa);
                    haveFog = true;
                }
                break;
            }
            if (haveCam) {
                FrameCamera fc;
                fc.viewProj = vp;
                fc.overlayProj = haveFog ? ov : nullptr;
                fc.fowAnim = haveFog ? fa : nullptr;
                fc.width = scene0.targets[ti].width;
                fc.height = scene0.targets[ti].height;
                CameraShift shift;
                applyFrameCamera(sc, ti, fc, tapRows, &shift);
            }
            const double a2 = qpcMsNow();
            ViewInput view;
            view.pass = scene0.targets[ti].width == scene0.targets[ti].height && scene0.targets[ti].width == 1024
                            ? DrawPass::Reflection
                            : DrawPass::Scene;
            view.target = ti;
            view.rtv = tg[ti].rtv;
            view.dsv = tg[ti].dsv;
            view.viewport = {0, 0, static_cast<float>(scene0.targets[ti].width),
                             static_cast<float>(scene0.targets[ti].height), 0, 1};
            ctx->End(tg[ti].ts0);
            DrawReport rep;
            s = mr.draw(ctx, sc, view, env, &rep);
            ctx->End(tg[ti].ts1);
            const double a3 = qpcMsNow();
            if (!s.ok()) {
                ctx->End(dj);
                cleanup();
                return fail("bench draw", s);
            }
            r.push_back(a1 - a0);
            r.push_back(a2 - a1);
            r.push_back(rep.uploadMs);
            r.push_back(rep.itemsMs);
            r.push_back(a3 - a2);
            r.push_back(0);   // gpu, filled below
            r.push_back(rep.contextCalls);
            r.push_back(rep.drawn);
        }
        ctx->End(dj);
        const double q5 = qpcMsNow();
        ctx->Flush();
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d = {};
        while (ctx->GetData(dj, &d, sizeof d, 0) == S_FALSE) SwitchToThread();
        double gpuTotal = 0;
        size_t col = 4;
        for (uint32_t ti = 0; ti < nt; ++ti) {
            if (!drawnTarget[ti]) continue;
            UINT64 a = 0, b = 0;
            while (ctx->GetData(tg[ti].ts0, &a, sizeof a, 0) == S_FALSE) SwitchToThread();
            while (ctx->GetData(tg[ti].ts1, &b, sizeof b, 0) == S_FALSE) SwitchToThread();
            const double g = d.Disjoint || !d.Frequency ? -1.0 : 1000.0 * static_cast<double>(b - a) / d.Frequency;
            r[col + 5] = g;
            gpuTotal += g;
            col += 8;
        }
        r.push_back(q5 - q0);
        r.push_back(gpuTotal);
        prev.swap(img);
        if (f >= warm) rows.push_back(r);
    }
    cleanup();
    std::ofstream o(tsvPath);
    for (size_t c = 0; c < cols.size(); ++c) o << (c ? "\t" : "") << cols[c];
    o << '\n';
    for (const auto& r : rows) {
        for (size_t c = 0; c < r.size(); ++c) o << (c ? "\t" : "") << r[c];
        o << '\n';
    }
    for (size_t c = 0; c < cols.size(); ++c) {
        std::vector<double> v;
        for (const auto& r : rows) v.push_back(r[c]);
        const Stat st = statOf(v);
        std::printf("bench %-18s n %zu mean %.4f sd %.4f median %.4f p95 %.4f min %.4f max %.4f ci95 %.4f\n", cols[c].c_str(),
                    v.size(), st.mean, st.sd, st.median, st.p95, st.mn, st.mx, st.ci95);
    }
    return 0;
}

int cmdRun(int argc, char** argv) {
    std::map<std::string, std::string> a;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::strncmp(argv[i], "--", 2) != 0) return usage("options are --name value");
        a[argv[i] + 2] = argv[i + 1];
    }
    for (const char* k : {"game", "db", "exe", "frame", "tap", "out"})
        if (!a.count(k)) return usage((std::string("missing --") + k).c_str());
    const int onlyTarget = a.count("target") ? std::stoi(a["target"]) : -1;
    const int bench = a.count("bench") ? std::stoi(a["bench"]) : 0;   // ticket 4884
    if (bench < 0 || bench > 100000) return usage("--bench is 0..100000 frames");
    const std::string inputs = a.count("inputs") ? a["inputs"] : "capture";
    if (inputs != "capture" && inputs != "install" && inputs != "none") return usage("--inputs is capture, install or none");
    const std::string out = a["out"];
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    FrameList fl;
    std::string why;
    if (!parseFrame(a["frame"], fl, why)) {
        std::fprintf(stderr, "cityscene run: %s\n", why.c_str());
        return 1;
    }
    std::vector<uint8_t> tapBytes;
    TapFrame tf;
    Status s = readFileBytes(a["tap"], tapBytes);
    if (s.ok()) s = readTap(tapBytes.data(), tapBytes.size(), tf);
    if (!s.ok()) return fail("tap", s);

    gamefs::GameFiles files;
    s = files.open(gamefs::fromUtf8(a["game"]));
    if (!s.ok()) return fail("install", s);
    if (!c5m::SqliteTables::available()) return fail("database", Status::fail(Err::NotFound, "winsqlite3.dll"));
    c5m::SqliteTables db;
    c5m::Error e;
    if (!db.open(a["db"], &e)) return fail("database", Status::fail(Err::Io, e.what));
    cities::ArtCatalogue art;
    s = art.load(files, db);
    if (!s.ok()) return fail("art catalogue", s);
    ModelLibrary lib;
    s = lib.load(files, db, art);
    if (!s.ok()) return fail("library", s);
    WireIndex index(lib, files);
    s = index.build();
    if (!s.ok()) return fail("wire index", s);
    std::printf("library files %zu (ok %u) parts %u; wire index %.1f s, failed files %u, missing textures %u\n",
                lib.files().size(), lib.counts().filesOk, lib.counts().parts, index.buildSeconds(), index.failedFiles(),
                index.missingTextures());

    ModelScene scene;
    ResolveReport rr;
    s = resolve(index, lib, tf, scene, &rr);
    if (!s.ok()) return fail("resolve", s);
    std::printf("resolve draws %u resolved %u unknown %u (texture miss %u, material miss %u) ambiguous %u\n", rr.draws,
                rr.resolved, rr.unknown, rr.textureMiss, rr.materialMiss, rr.ambiguous);
    // Ticket 4879: the CPU-built meshes (walls, the Great Wall, the road and rail bridge): their R32 index buffer and
    // vertex buffer taken from the capture's own buffers, as the compositor copies them from the EXE at run time into the
    // geometry section (city-renderer.md 13); --cpu 0 leaves them unknown (4873's run).
    CpuMeshStore cpu;
    CpuResolveReport cr;
    const bool cpuOn = !a.count("cpu") || a["cpu"] != "0";
    if (cpuOn) {
        FileCache gc;
        uint32_t refused = 0;
        for (const DrawDesc& d : fl.draws) {
            if (d.ibBytes != 4 || (d.mat != 3 && d.mat != 5)) continue;
            const std::vector<uint8_t>* ib = gc.get(d.ib);
            const std::vector<uint8_t>* vb = gc.get(d.vb);
            if (!ib || !vb) continue;
            if (!cpu.add(GeomKind::Index32, geometryHash(GeomKind::Index32, ib->data(), ib->size()), 4, ib->data(), ib->size()).ok())
                ++refused;
            if (!cpu.add(GeomKind::Vertex, geometryHash(GeomKind::Vertex, vb->data(), vb->size()), d.vbStride, vb->data(),
                         vb->size())
                     .ok())
                ++refused;
        }
        const uint32_t named = addCpuTextureNames(index);
        s = resolveCpu(cpu, index, tf, scene, &cr);
        if (!s.ok()) return fail("resolve cpu", s);
        std::printf("cpu-built buffers held %zu (%llu bytes, refused %u); bridge maps named %u; r32 draws %u resolved %u "
                    "no geometry %u bad stride %u texture unnamed %u; unknown now %u\n",
                    cpu.size(), static_cast<unsigned long long>(cpu.bytes()), refused, named, cr.r32, cr.resolved,
                    cr.noGeometry, cr.badStride, cr.textureUnnamed, scene.unknownDraws);
    }
    std::vector<uint8_t> sceneBytes;
    s = writeScene(scene, lib, sceneBytes);
    if (s.ok()) s = writeFileBytes(out + "/scene.c5models", sceneBytes);
    ModelScene back;
    if (s.ok()) s = readScene(sceneBytes.data(), sceneBytes.size(), lib, back);
    if (!s.ok()) return fail("scene file", s);
    bool same = back.items.size() == scene.items.size() && back.rows.size() == scene.rows.size();
    for (size_t i = 0; same && i < scene.items.size(); ++i)
        same = back.items[i].file == scene.items[i].file && back.items[i].part == scene.items[i].part &&
               back.items[i].instRow == scene.items[i].instRow && back.items[i].tapDraw == scene.items[i].tapDraw;
    if (!same) return fail("scene file", Status::fail(Err::BadLayout, "round trip unequal"));

    {
        std::ofstream it(out + "/items.tsv");
        it << "item\ttap_draw\tn\tmaterial\tinstances\tgr2\tscale\tpart\tmesh\tt0\tt1\n";
        for (size_t i = 0; i < scene.items.size(); ++i) {
            const DrawItem& d = scene.items[i];
            const ModelFile& mf = lib.files()[d.file];
            const Part& p = mf.layout.parts[d.part];
            it << i << '\t' << d.tapDraw << '\t' << fl.draws[d.tapDraw].n << '\t' << static_cast<int>(d.material) << '\t'
               << d.instanceCount << '\t' << mf.gr2 << '\t' << mf.scale << '\t' << d.part << '\t' << p.mesh << '\t'
               << p.t0 << '\t' << p.t1 << '\n';
        }
        for (size_t ci = 0; ci < scene.cpuItems.size(); ++ci) {   // ticket 4879: ids after the files' items
            const CpuDrawItem& d = scene.cpuItems[ci];
            it << scene.items.size() + ci << '\t' << d.tapDraw << '\t' << fl.draws[d.tapDraw].n << '\t'
               << static_cast<int>(d.material) << '\t' << d.instanceCount << "\tcpu-built\t1\t-1\t-\t" << d.t0 << '\t' << d.t1
               << '\n';
        }
        // the PS t2-t4 sets by (material, target): does class + pass fix them (lead2 #6108)?
        std::map<std::string, uint32_t> envSets;
        for (const TapDraw& d : tf.draws)
            ++envSets[std::to_string(static_cast<int>(d.material)) + '\t' + std::to_string(d.target) + '\t' +
                      hex64(d.env[0]) + '\t' + hex64(d.env[1]) + '\t' + hex64(d.env[2])];
        std::ofstream es(out + "/env.tsv");
        es << "material\ttarget\tt2\tt3\tt4\tdraws\n";
        for (auto& kv : envSets) es << kv.first << '\t' << kv.second << '\n';
        std::ofstream un(out + "/unknown.tsv");
        un << "tap_draw\tn\tmaterial\tib\tvb\tt0\tt1\tstart\tcount\tbase\tgeometry_candidates\n";
        for (uint32_t ti : scene.unknown) {
            const TapDraw& d = tf.draws[ti];
            un << ti << '\t' << fl.draws[ti].n << '\t' << static_cast<int>(d.material) << '\t' << hex64(d.key.ib) << '\t'
               << hex64(d.key.vb) << '\t' << hex64(d.key.t0) << '\t' << hex64(d.key.t1) << '\t' << d.key.startIndex
               << '\t' << d.key.indexCount << '\t' << d.key.baseVertex << '\t';
            for (const ModelLibrary::Hit& h : index.findGeometry(d.key)) {
                const ModelFile& mf = lib.files()[h.file];
                const Part& p = mf.layout.parts[h.part];
                const WireKey* k = index.key(h.file, h.part);
                un << mf.gr2 << ':' << h.part << '[' << p.t0 << '=' << hex64(k ? k->t0 : 0) << ',' << p.t1 << '='
                   << hex64(k ? k->t1 : 0) << "] ";
            }
            un << '\n';
        }
    }

    Gpu gpu;
    D3D_FEATURE_LEVEL fl11 = D3D_FEATURE_LEVEL_11_0, got;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl11, 1, D3D11_SDK_VERSION, &gpu.dev,
                                 &got, &gpu.ctx)))
        return fail("device", Status::fail(Err::Device, "D3D11CreateDevice"));
    BuildingPrograms programs;
    s = programs.open(gamefs::fromUtf8(a["exe"]));
    if (!s.ok()) return fail("programs", s);
    ModelRenderer mr;
    s = mr.init(gpu.dev, programs, lib, files);
    if (!s.ok()) return fail("renderer", s);
    if (cpuOn) mr.setCpuMeshes(&cpu);   // ticket 4879

    // The capture's own PS t2-t9 (like for like; the server's own are 4875's).
    FileCache fc;
    std::map<std::string, std::pair<ID3D11Resource*, ID3D11ShaderResourceView*>> made;
    auto srvOf = [&](const std::string& id) -> ID3D11ShaderResourceView* {
        if (id == "-" || id.empty()) return nullptr;
        auto m = made.find(id);
        if (m != made.end()) return m->second.second;
        auto t = fl.textures.find(id);
        ID3D11Resource* r = nullptr;
        ID3D11ShaderResourceView* v = nullptr;
        if (t != fl.textures.end()) {
            Status ts = createExportTexture(gpu.dev, t->second, fc, &r, &v);
            if (!ts.ok()) std::fprintf(stderr, "cityscene run: %s\n", ts.str().c_str());
        }
        made[id] = {r, v};
        return v;
    };
    // ticket 4882: the server's own sources of t2-t6 and t9 (--inputs install)
    InstallInputs ii;
    if (inputs == "install") {
        s = ii.open(files);
        if (s.ok()) s = ii.create(gpu.dev);
        if (!s.ok()) return fail("install inputs", s);
        const InstallInputs::Counts& c = ii.counts();
        std::printf("inputs install: cube candidates %u cubes %u identities %u; buildings volume from file %d, fow volume "
                    "from file %d; %.0f ms\n",
                    c.candidates, c.cubes, c.identities, c.buildingsFromFile ? 1 : 0, c.fowFromFile ? 1 : 0, c.openMs);
    }
    // the bytes uploaded against the capture's file of the same slot (all mips, as RenderDoc stored them)
    auto compareSlot = [&](int slot, const std::string& texId, const std::vector<uint8_t>& ours) {
        auto t = fl.textures.find(texId);
        std::vector<uint8_t> cap;
        const bool have = t != fl.textures.end() && readFileBytes(t->second.path, cap).ok();
        const std::string a1 = units::sha256Hex(ours.data(), ours.size());
        const std::string a2 = have ? units::sha256Hex(cap.data(), cap.size()) : std::string("-");
        // the top level alone too (the capture stores each level tight; a small level's row pitch may differ)
        const size_t top = have && !t->second.sizes.empty() ? t->second.sizes[0] : 0;
        const bool topEqual = top && top <= ours.size() && top <= cap.size() && std::memcmp(ours.data(), cap.data(), top) == 0;
        std::printf("inputs t%d ours %zu B %.16s capture %zu B %.16s equal %d top_level %zu B equal %d\n", slot, ours.size(),
                    a1.c_str(), cap.size(), a2.c_str(), have && a1 == a2 ? 1 : 0, top, topEqual ? 1 : 0);
    };
    int rc = 0;
    std::vector<EnvInput> envs(scene.targets.size());   // ticket 4884: each target's inputs, for --bench
    std::vector<bool> drawnTarget(scene.targets.size(), false);
    for (uint32_t ti = 0; ti < scene.targets.size(); ++ti) {
        if (onlyTarget >= 0 && static_cast<uint32_t>(onlyTarget) != ti) continue;
        const TapTarget& tt = scene.targets[ti];
        EnvInput env;
        const DrawDesc* first = nullptr;
        uint32_t frameDiffers = 0, envCapture = 0, envInstall = 0;
        for (const DrawDesc& d : fl.draws) {
            if (d.target != ti) continue;
            if (!first) first = &d;
            for (int k = 5; k < 10; ++k)
                if (d.tex[k] != first->tex[k]) {
                    ++frameDiffers;
                    break;
                }
            for (int k = 2; k < 5; ++k) {
                const uint64_t id = tf.draws[d.idx].env[k - 2];
                if (!id || env.env.count(id)) continue;
                ++envCapture;
                if (inputs == "capture") env.env[id] = srvOf(d.tex[k]);
                if (inputs == "install") {
                    ID3D11ShaderResourceView* v = ii.env(id);
                    if (v) {
                        env.env[id] = v;
                        ++envInstall;
                    } else {
                        std::printf("inputs t%d identity %016llx of draw %u: no install cube\n", k,
                                    static_cast<unsigned long long>(id), d.n);
                    }
                }
            }
        }
        if (!first) continue;
        if (inputs == "capture") {
            for (int k = 0; k < 5; ++k) env.frame[k] = srvOf(first->tex[5 + k]);
        } else if (inputs == "install") {
            ii.fill(scene, ti, env);
            env.frame[2] = srvOf(first->tex[7]);
            env.frame[3] = srvOf(first->tex[8]);
            std::printf("inputs target %u: env identities %u, from the install %u\n", ti, envCapture, envInstall);
            compareSlot(5, first->tex[5], ii.rampBytes());
            compareSlot(6, first->tex[6], ii.buildingsBytes());
            compareSlot(9, first->tex[9], ii.fowBytes());
        }
        envs[ti] = env;
        drawnTarget[ti] = true;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = tt.width;
        td.Height = tt.height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D *rt = nullptr, *idt = nullptr, *dt = nullptr;
        ID3D11RenderTargetView *rtv = nullptr, *idv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        bool okT = SUCCEEDED(gpu.dev->CreateTexture2D(&td, nullptr, &rt)) &&
                   SUCCEEDED(gpu.dev->CreateRenderTargetView(rt, nullptr, &rtv));
        td.Format = DXGI_FORMAT_R32_UINT;
        okT = okT && SUCCEEDED(gpu.dev->CreateTexture2D(&td, nullptr, &idt)) &&
              SUCCEEDED(gpu.dev->CreateRenderTargetView(idt, nullptr, &idv));
        td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;   // as terrainreplay's reference target
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        okT = okT && SUCCEEDED(gpu.dev->CreateTexture2D(&td, nullptr, &dt)) &&
              SUCCEEDED(gpu.dev->CreateDepthStencilView(dt, nullptr, &dsv));
        if (!okT) {
            rc = fail("targets", Status::fail(Err::Device, "creating the targets"));
            break;
        }
        const float clear[4] = {0, 0, 0, 0};
        gpu.ctx->ClearRenderTargetView(rtv, clear);
        gpu.ctx->ClearRenderTargetView(idv, clear);   // R32_UINT: 0.0 clears to 0 (no item)
        gpu.ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        ViewInput view;
        view.pass = tt.width == tt.height && tt.width == 1024 ? DrawPass::Reflection : DrawPass::Scene;
        view.target = ti;
        view.rtv = rtv;
        view.dsv = dsv;
        view.idRtv = idv;
        view.viewport = {0, 0, static_cast<float>(tt.width), static_cast<float>(tt.height), 0, 1};
        DrawReport rep;
        const auto d0 = clock::now();
        s = mr.draw(gpu.ctx, scene, view, env, &rep);
        gpu.ctx->Flush();
        std::vector<uint8_t> px, ids;
        if (s.ok()) s = readBack(gpu.dev, gpu.ctx, rt, 4, px);
        const double drawMs = std::chrono::duration<double, std::milli>(clock::now() - d0).count();
        if (s.ok()) s = readBack(gpu.dev, gpu.ctx, idt, 4, ids);
        if (s.ok()) s = writeFileBytes(out + "/ours_" + std::to_string(ti) + ".rgba", px);
        if (s.ok()) s = writeFileBytes(out + "/ours_" + std::to_string(ti) + "_id.u32", ids);
        rel(rtv);
        rel(idv);
        rel(dsv);
        rel(rt);
        rel(idt);
        rel(dt);
        if (!s.ok()) {
            rc = fail("draw", s);
            break;
        }
        std::printf("target %u %ux%u pass %s items %u drawn %u skipped %u env_missing %u texture_missing %u "
                    "buffer_failed %u frame_textures_differ %u draw+readback %.1f ms\n",
                    ti, tt.width, tt.height, view.pass == DrawPass::Scene ? "scene" : "reflection", rep.items, rep.drawn,
                    rep.skipped, rep.envMissing, rep.textureMissing, rep.bufferFailed, frameDiffers, drawMs);
        for (const std::string& p : rep.problems) std::printf("  problem %s\n", p.c_str());
    }
    if (rc == 0 && bench > 0) rc = runBench(gpu.dev, gpu.ctx, tapBytes, index, lib, cpuOn ? &cpu : nullptr, mr,
                                            inputs == "install" ? &ii : nullptr, scene, envs, drawnTarget, bench,
                                            out + "/bench.tsv");
    for (auto& kv : made) {
        rel(kv.second.second);
        rel(kv.second.first);
    }
    std::printf("gpu bytes %.1f MB textures %u total %.1f s\n", mr.gpuBytes() / 1048576.0, mr.textures(),
                std::chrono::duration<double>(clock::now() - t0).count());
    return rc;
}

// cityscene params INSTALL GR2...: every material's maps and parameter sets of the named files (a look, not a test).
int cmdParams(int argc, char** argv) {
    if (argc < 4) return usage("params takes INSTALL GR2...");
    gamefs::GameFiles files;
    Status s = files.open(gamefs::fromUtf8(argv[2]));
    if (!s.ok()) return fail("install", s);
    c5m::StringDatabase sdb;
    units::GameFilesProvider prov(files);
    units::loadStringDatabases(files.names(), prov, sdb);
    for (int i = 3; i < argc; ++i) {
        std::vector<uint8_t> b;
        std::string origin;
        if (!files.read(argv[i], b, &origin).ok()) {
            std::printf("%s: not in the install\n", argv[i]);
            continue;
        }
        c5m::FileInfo fi;
        c5m::Error e;
        if (!c5m::read_gr2(b.data(), b.size(), &sdb, fi, &e)) {
            std::printf("%s: %s\n", argv[i], e.what.c_str());
            continue;
        }
        std::vector<std::vector<MaterialParam>> params;
        s = materialParams(b.data(), b.size(), &sdb, params);
        std::printf("%s (%s): %zu materials%s\n", argv[i], origin.c_str(), fi.materials.size(),
                    s.ok() ? "" : (" params: " + s.str()).c_str());
        for (size_t m = 0; m < fi.materials.size(); ++m) {
            const c5m::Material& mt = fi.materials[m];
            std::printf("  material %zu %s [%s]\n", m, mt.name.c_str(), mt.shader_set.c_str());
            for (const c5m::TextureMap& t : mt.maps) std::printf("    map %s = %s\n", t.usage.c_str(), t.basename.c_str());
            if (m < params.size())
                for (const MaterialParam& p : params[m])
                    std::printf("    param %s.%s = %s\n", p.block.c_str(), p.name.c_str(), p.value.c_str());
        }
    }
    return 0;
}

// cityscene texid INSTALL SUBSTRING...: every install .dds whose name holds one of the substrings, with its format,
// size and texture identity (the PS t2-t4 cubes named against the tap's env identities; the BGRA rule checked).
int cmdTexid(int argc, char** argv) {
    if (argc < 4) return usage("texid takes INSTALL SUBSTRING...");
    gamefs::GameFiles files;
    Status s = files.open(gamefs::fromUtf8(argv[2]));
    if (!s.ok()) return fail("install", s);
    ModelLibrary lib;
    WireIndex index(lib, files);
    for (const std::string& n : files.names()) {
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".dds") != 0) continue;
        bool hit = false;
        for (int i = 3; i < argc && !hit; ++i) hit = n.find(argv[i]) != std::string::npos;
        if (!hit) continue;
        std::vector<uint8_t> b;
        gamefs::DdsImage img;
        if (!files.read(n, b).ok() || !gamefs::parseDds(b.data(), b.size(), img).ok()) {
            std::printf("%s\tunreadable\n", n.c_str());
            continue;
        }
        std::printf("%s\t%s\t%ux%u\tmips %u\tarray %u%s\t%s\n", n.c_str(), gamefs::dxgiFormatName(img.format).c_str(),
                    img.width, img.height, img.mipLevels, img.arraySize, img.cube ? " cube" : "",
                    hex64(index.textureId(n)).c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage("no command");
    const std::string cmd = argv[1];
    if (cmd == "tap") return cmdTap(argc, argv);
    if (cmd == "run") return cmdRun(argc, argv);
    if (cmd == "params") return cmdParams(argc, argv);
    if (cmd == "texid") return cmdTexid(argc, argv);
    return usage("unknown command");
}
