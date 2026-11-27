// c5r::ice on the huge map's ice mesh (issue 4310, epic 0169; task 4306's open item): the mesh the ice tap of task 4307
// published on a huge map (8 chunks; reference set F:/c5-64bit-work/0169/4307/ref_huge, manifest
// shell/evidence/0169/4307/ref_huge_manifest.json) goes through setMesh unchanged, and the draw issues one DrawIndexed per
// chunk with that chunk's range (index count = 3 x triangles, start index, base vertex), in the EXE's order, per map copy;
// the cull keeps exactly the chunks whose box meets the cull box; a mesh refused afterwards keeps the 8-chunk mesh; the
// picture is the same on two hardware devices. D137: offline, no game client. Own code; the EXE, the install and both
// reference sets are read at run time.
//
// Inputs: C5R_ICE_HUGE (default F:/c5-64bit-work/0169/4307/ref_huge: ice.c5ice, the tap's file form, and its parts
// vb.bin, ib.bin, fields.bin); C5R_ICE_REF (task 4301's set: the frame's shared inputs t7-t10, ice_util.h); C5R_EXE and
// C5R_GAME_DIR as the other ice tests. A missing huge set FAILS (never skips): set C5R_ICE_HUGE to its folder, e.g.
// `set C5R_ICE_HUGE=F:/c5-64bit-work/0169/4307/ref_huge`.
//
// How the draws are seen: the test points the DrawIndexed slot of the runtime's context table at a recorder of (count,
// start, base) that forwards to the runtime (DrawHook below); the recorded calls are checked against the module's own
// count. The slot is restored when the last hook goes.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "c5r/gamefs/gamefiles.h"
#include "c5r/ice/ice.h"
#include "c5r/shaders/shadersection.h"
#include "ice_util.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::ice;
using namespace c5r::itest;

namespace {

// ---- the huge set ----

const uint32_t kHugeChunks = 8;                  // [L, 4307: the huge save C5H-VP8P-HUGE_0350's mesh]
const uint32_t kImageMagic = 0x43493543u;        // 'C5IC' (shell/common/shell_ice_tap.h VPSHELL_ICE_MAGIC)
const uint32_t kImageHeader = 112, kChunkBytes = 32;
const float kHugeWorldWidth = 8191.0f;           // (128 * 64 - 1) * HexScale.x 1.0 [I, as Tiny's 3583 = 56 * 64 - 1]

std::string hugeDir() {
    const char* e = std::getenv("C5R_ICE_HUGE");
    const std::string d = e && *e ? e : "F:/c5-64bit-work/0169/4307/ref_huge";
    if (GetFileAttributesW(wide(d + "/ice.c5ice").c_str()) == INVALID_FILE_ATTRIBUTES) {
        test::fail(__FILE__, __LINE__, "the huge ice set of task 4307 is not at " + d + " (no ice.c5ice): set C5R_ICE_HUGE to its "
                                       "folder, e.g. set C5R_ICE_HUGE=F:/c5-64bit-work/0169/4307/ref_huge (D137; never skipped)");
        return std::string();
    }
    return d;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

// The huge mesh from the tap's file form (VPShellIceImage, VPShellIceChunk), checked against the set's loose parts; the
// image's chunks in their order.
bool hugeMesh(Mesh& m) {
    m = Mesh();
    const std::string d = hugeDir();
    if (d.empty()) return false;
    std::vector<uint8_t> img, vb, ib, fields;
    if (!readFile(d + "/ice.c5ice", img) || !readFile(d + "/vb.bin", vb) || !readFile(d + "/ib.bin", ib) ||
        !readFile(d + "/fields.bin", fields)) {
        test::fail(__FILE__, __LINE__, "the huge set at " + d + " lacks ice.c5ice, vb.bin, ib.bin or fields.bin");
        return false;
    }
    CHECK(img.size() >= kImageHeader);
    if (img.size() < kImageHeader) return false;
    const uint32_t magic = rd32(img, 0), version = rd32(img, 4), header = rd32(img, 8), chunkBytes = rd32(img, 12);
    const uint32_t chunks = rd32(img, 28), fieldsBase = rd32(img, 32), fieldsBytes = rd32(img, 36), stride = rd32(img, 40);
    const uint32_t ibFormat = rd32(img, 44), vbBytes = rd32(img, 48), ibBytes = rd32(img, 52);
    const uint32_t fieldsOff = rd32(img, 56), chunkOff = rd32(img, 60), vbOff = rd32(img, 64), ibOff = rd32(img, 68);
    const uint32_t imageBytes = rd32(img, 72);
    CHECK_EQ(magic, kImageMagic);
    CHECK_EQ(version, 1u);
    CHECK_EQ(header, kImageHeader);
    CHECK_EQ(chunkBytes, kChunkBytes);
    CHECK_EQ(chunks, kHugeChunks);
    CHECK_EQ(fieldsBase, kFieldsBase);
    CHECK_EQ(fieldsBytes, kFieldsSize);
    CHECK_EQ(stride, kIceStride);
    CHECK_EQ(ibFormat, static_cast<uint32_t>(DXGI_FORMAT_R16_UINT));
    CHECK_EQ(static_cast<size_t>(imageBytes), img.size());
    const bool inside = static_cast<uint64_t>(fieldsOff) + fieldsBytes <= img.size() &&
                        static_cast<uint64_t>(chunkOff) + static_cast<uint64_t>(chunks) * kChunkBytes <= img.size() &&
                        static_cast<uint64_t>(vbOff) + vbBytes <= img.size() && static_cast<uint64_t>(ibOff) + ibBytes <= img.size();
    CHECK(inside);
    if (!inside || chunks != kHugeChunks || fieldsBytes != kFieldsSize || magic != kImageMagic) return false;
    m.vb.assign(img.begin() + vbOff, img.begin() + vbOff + vbBytes);
    m.ib.assign(img.begin() + ibOff, img.begin() + ibOff + ibBytes);
    // the set's loose parts are the image's sections byte for byte (the manifest's SHA-256s name both)
    CHECK(m.vb == vb);
    CHECK(m.ib == ib);
    CHECK(fields.size() == kFieldsSize && std::memcmp(fields.data(), img.data() + fieldsOff, kFieldsSize) == 0);
    std::memcpy(m.fields.raw, img.data() + fieldsOff, kFieldsSize);
    m.fields.valid = true;
    for (uint32_t i = 0; i < chunks; ++i) {
        const size_t o = chunkOff + static_cast<size_t>(i) * kChunkBytes;
        Chunk c;
        std::memcpy(c.box, img.data() + o, 16);
        c.triangles = rd32(img, o + 16);
        c.startIndex = rd32(img, o + 20);
        std::memcpy(&c.baseVertex, img.data() + o + 24, 4);
        m.chunks.push_back(c);
    }
    return true;
}

// ---- the DrawIndexed recorder ----

struct DrawRec {
    UINT count, start;
    INT base;
};
typedef void(STDMETHODCALLTYPE* DrawIndexedFn)(ID3D11DeviceContext*, UINT, UINT, INT);
const size_t kDrawIndexedSlot = 12;   // IUnknown 3 + ID3D11DeviceChild 4 + VSSetConstantBuffers .. VSSetShader 5

// Records the DrawIndexed calls on one context. The hook writes the DrawIndexed slot of the runtime's table itself (made
// writable for the write, in this test process only; restored when the table's last hook goes); the context object and
// its vtable pointer stay the runtime's; calls on contexts without a hook only pass through. No draw of its own (an empty
// DrawIndexed with nothing bound hung the device, DXGI_ERROR_DEVICE_HUNG, 2026-11-15): the tests check the recorded calls
// against the module's own count (FrameStats::draws), so a wrong slot fails them.
struct DrawHook {
    ID3D11DeviceContext* ctx = nullptr;
    std::vector<DrawRec> rec;
    bool install(ID3D11DeviceContext* c);
    ~DrawHook();
};
DrawHook* g_hooks[4] = {};
struct Patched {
    void** table = nullptr;        // a runtime table whose slot holds recordDrawIndexed
    DrawIndexedFn orig = nullptr;  // the runtime's DrawIndexed of that table
    uint32_t users = 0;
};
Patched g_patched[4];

void STDMETHODCALLTYPE recordDrawIndexed(ID3D11DeviceContext* c, UINT n, UINT s, INT b) {
    for (DrawHook* h : g_hooks)
        if (h && h->ctx == c) h->rec.push_back(DrawRec{n, s, b});
    void** vt = *reinterpret_cast<void***>(c);
    for (const Patched& p : g_patched)
        if (p.table == vt) {
            p.orig(c, n, s, b);
            return;
        }
    std::fprintf(stderr, "recordDrawIndexed: no patched table %p\n", static_cast<void*>(vt));
    std::abort();
}

bool writeSlot(void** table, void* fn) {
    DWORD prot = 0;
    if (!VirtualProtect(&table[kDrawIndexedSlot], sizeof(void*), PAGE_READWRITE, &prot)) return false;
    table[kDrawIndexedSlot] = fn;
    DWORD unused = 0;
    VirtualProtect(&table[kDrawIndexedSlot], sizeof(void*), prot, &unused);
    return true;
}

bool DrawHook::install(ID3D11DeviceContext* c) {
    DrawHook** slot = nullptr;
    for (DrawHook*& h : g_hooks)
        if (!h) {
            slot = &h;
            break;
        }
    if (!slot) return false;
    void** vt = *reinterpret_cast<void***>(c);
    Patched* pt = nullptr;
    for (Patched& p : g_patched)
        if (p.table == vt) pt = &p;
    if (!pt) {
        for (Patched& p : g_patched)
            if (!p.table) {
                pt = &p;
                break;
            }
        if (!pt) return false;
        pt->orig = reinterpret_cast<DrawIndexedFn>(vt[kDrawIndexedSlot]);
        if (!writeSlot(vt, reinterpret_cast<void*>(&recordDrawIndexed))) return false;
        pt->table = vt;
    }
    ++pt->users;
    ctx = c;
    *slot = this;
    return true;
}

DrawHook::~DrawHook() {
    if (!ctx) return;
    for (DrawHook*& h : g_hooks)
        if (h == this) h = nullptr;
    void** vt = *reinterpret_cast<void***>(ctx);
    for (Patched& p : g_patched)
        if (p.table == vt && --p.users == 0) {
            writeSlot(p.table, reinterpret_cast<void*>(p.orig));
            p = Patched();
        }
}

// ---- the scene ----

struct Inputs {
    shaders::ShaderSection sec;
    gamefs::GameFiles fs;
    bool ok = false;
};
Inputs& inputs() {
    static Inputs* in = nullptr;
    if (!in) {
        in = new Inputs();
        const std::string exe = test::exePath(), game = test::gameDir();
        if (exe.empty()) test::skip("C5R_EXE not set: the IceShader programs come from the EXE");
        if (game.empty()) test::skip("C5R_GAME_DIR not set: the ice's textures come from the install");
        Status a = in->sec.open(wide(exe)), b = in->fs.open(wide(game));
        CHECK_OK(a);
        CHECK_OK(b);
        in->ok = a.ok() && b.ok();
    }
    return *in;
}

// A top-down orthographic camera over the mesh's whole box (row vectors, as the EXE's matrices: translation in row 3;
// x right, y up, as stock's camera, so the windings stay front-facing; a higher z is nearer).
FrameInputs topDown(const Mesh& m, uint32_t w, uint32_t h) {
    FrameInputs in;
    const float x0 = m.fields.get(field::kBox), y0 = m.fields.get(field::kBox + 4), x1 = m.fields.get(field::kBox + 8),
                y1 = m.fields.get(field::kBox + 12);
    const float sx = 1.96f / (x1 - x0), sy = 1.96f / (y1 - y0);
    float v[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -0.001f, 0, -0.5f * (x0 + x1) * sx, -0.5f * (y0 + y1) * sy, 0.5f, 1};
    for (int i = 0; i < 16; ++i) {
        in.cam.view[i] = v[i];
        in.cam.proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    in.cam.width = w;
    in.cam.height = h;
    in.worldWidth = kHugeWorldWidth;
    return in;
}

uint32_t covered(const std::vector<uint8_t>& rgba) {
    uint32_t n = 0;
    for (size_t i = 0; i < rgba.size(); i += 4)
        if (rgba[i] | rgba[i + 1] | rgba[i + 2] | rgba[i + 3]) ++n;
    return n;
}

// One device with the module, the huge mesh, the shared inputs, a target and the hook.
struct Run {
    Device d;
    Renderer r;
    Target t;
    Shared sh;
    Mesh mesh;
    FrameInputs in;
    FrameStats st;
    DrawHook hook;   // last: unregistered first
    bool ready = false;
    bool start(uint32_t w, uint32_t h) {
        Inputs& g = inputs();
        if (!g.ok || !hugeMesh(mesh)) return false;
        Status s = d.create();
        CHECK_OK(s);
        if (!s.ok()) return false;
        s = r.init(d.dev, g.sec, g.fs);
        CHECK_OK(s);
        if (!s.ok()) return false;
        s = r.setMesh(mesh);   // the tap's mesh unchanged
        CHECK_OK(s);
        if (!s.ok()) return false;
        CHECK_EQ(r.chunkCount(), kHugeChunks);
        if (!sh.make(d.dev, d.ctx)) return false;
        in = topDown(mesh, w, h);
        sh.into(in);
        CHECK_OK(t.create(d.dev, w, h));
        const bool hooked = hook.install(d.ctx);
        CHECK(hooked);
        ready = hooked;
        return ready;
    }
    bool draw(const FrameInputs& fi, std::vector<uint8_t>* rgba) {
        hook.rec.clear();
        t.clear(d.ctx);
        Status s = r.draw(d.ctx, t.rtv, t.dsv, fi, &st);
        CHECK_OK(s);
        if (!s.ok()) return false;
        if (rgba) {
            const Status rs = t.read(d.ctx, *rgba);
            CHECK_OK(rs);
            if (!rs.ok()) std::printf("device removed reason 0x%08lx\n", static_cast<unsigned long>(d.dev->GetDeviceRemovedReason()));
        }
        return true;
    }
};

// The recorded draws are the chunks' ranges, in order, once per copy.
void checkRanges(const std::vector<DrawRec>& rec, const Mesh& m, uint32_t copies) {
    CHECK_EQ(rec.size(), static_cast<size_t>(copies) * m.chunks.size());
    if (rec.size() != static_cast<size_t>(copies) * m.chunks.size()) return;
    for (size_t i = 0; i < rec.size(); ++i) {
        const Chunk& c = m.chunks[i % m.chunks.size()];
        if (rec[i].count != c.triangles * 3 || rec[i].start != c.startIndex || rec[i].base != c.baseVertex)
            test::fail(__FILE__, __LINE__, "draw " + std::to_string(i) + ": DrawIndexed(" + std::to_string(rec[i].count) + ", " +
                                               std::to_string(rec[i].start) + ", " + std::to_string(rec[i].base) + "), chunk wants (" +
                                               std::to_string(c.triangles * 3) + ", " + std::to_string(c.startIndex) + ", " +
                                               std::to_string(c.baseVertex) + ")");
    }
}

} // namespace

// setMesh takes the 8-chunk mesh as the tap published it; one draw issues 8 DrawIndexed with the chunks' ranges; the
// picture covers the ice and is the same on a second hardware device.
C5R_TEST(huge_mesh_accepted_and_drawn_per_chunk) {
    const uint32_t W = 1200, H = 668;
    Run a;
    if (!a.start(W, H)) return;
    // the set is what 4307 recorded: 8 chunks, 427,774 triangles, the chunks back to back in the IB
    uint64_t tris = 0;
    for (size_t i = 0; i < a.mesh.chunks.size(); ++i) {
        tris += a.mesh.chunks[i].triangles;
        if (i) CHECK_EQ(a.mesh.chunks[i].startIndex, a.mesh.chunks[i - 1].startIndex + a.mesh.chunks[i - 1].triangles * 3);
    }
    CHECK_EQ(tris, static_cast<uint64_t>(427774));
    CHECK_EQ(a.mesh.ib.size(), static_cast<size_t>(tris * 3 * 2));
    std::vector<uint8_t> pa, pb;
    if (!a.draw(a.in, &pa)) return;
    CHECK_EQ(a.st.copies, 1u);
    CHECK_EQ(a.st.draws, kHugeChunks);
    CHECK_EQ(a.st.chunksCulled, 0u);
    CHECK_EQ(a.st.indices, tris * 3);
    CHECK_EQ(a.r.frames().size(), static_cast<size_t>(1));
    checkRanges(a.hook.rec, a.mesh, 1);
    std::printf("huge ice: %zu DrawIndexed:", a.hook.rec.size());
    for (const DrawRec& d : a.hook.rec) std::printf(" (%u,%u,%d)", d.count, d.start, d.base);
    std::printf("\n");
    const uint32_t n = covered(pa);
    std::printf("huge ice draw: %u of %u pixels covered\n", n, W * H);
    CHECK(n > W * H / 100);
    Run b;
    if (!b.start(W, H) || !b.draw(b.in, &pb)) return;
    CHECK(pa == pb);
    if (pa != pb) {
        uint32_t diff = 0;
        for (size_t i = 0; i < pa.size(); ++i) diff += pa[i] != pb[i];
        test::fail(__FILE__, __LINE__, "the two devices differ in " + std::to_string(diff) + " bytes");
    }
    const std::string out = test::workDir() + "/ice_huge_topdown.bmp";
    if (writeBmp(out, pa, W, H)) std::printf("diagnostic picture: %s\n", out.c_str());
}

// Wrap: three copies of the 8 ranges (k = 0, 1, 2), 24 DrawIndexed. Cull (copy 0): a box inside chunk 5 alone keeps chunk
// 5's range only; a box away from the ice draws nothing.
C5R_TEST(huge_copies_and_cull_per_chunk) {
    Run a;
    if (!a.start(320, 180)) return;
    FrameInputs in = a.in;
    in.wrapX = true;
    if (!a.draw(in, nullptr)) return;
    CHECK_EQ(a.st.copies, 3u);
    CHECK_EQ(a.st.draws, 3 * kHugeChunks);
    checkRanges(a.hook.rec, a.mesh, 3);
    in.wrapX = false;
    in.haveCullBox = true;
    const Chunk& c5 = a.mesh.chunks[5];
    const float cx = 0.5f * (c5.box[0] + c5.box[2]), cy = 0.5f * (c5.box[1] + c5.box[3]);
    uint32_t meets = 0;
    const float box[4] = {cx - 10.0f, cy - 10.0f, cx + 10.0f, cy + 10.0f};
    for (const Chunk& c : a.mesh.chunks) meets += chunkVisible(c, box) ? 1u : 0u;
    CHECK_EQ(meets, 1u);   // the set's boxes: only chunk 5 holds that point
    std::memcpy(in.cullBox, box, sizeof box);
    if (!a.draw(in, nullptr)) return;
    CHECK_EQ(a.st.draws, 1u);
    CHECK_EQ(a.st.chunksCulled, kHugeChunks - 1);
    CHECK_EQ(a.hook.rec.size(), static_cast<size_t>(1));
    if (a.hook.rec.size() == 1) {
        CHECK_EQ(a.hook.rec[0].count, c5.triangles * 3);
        CHECK_EQ(a.hook.rec[0].start, c5.startIndex);
        CHECK_EQ(a.hook.rec[0].base, c5.baseVertex);
    }
    const float away[4] = {50000.0f, 50000.0f, 60000.0f, 60000.0f};
    std::memcpy(in.cullBox, away, sizeof away);
    std::vector<uint8_t> px;
    if (!a.draw(in, &px)) return;
    CHECK_EQ(a.st.draws, 0u);
    CHECK_EQ(a.hook.rec.size(), static_cast<size_t>(0));
    CHECK_EQ(covered(px), 0u);
}

// Negatives on the huge mesh: a last chunk reaching past the IB, a chunk whose base vertex puts indices past the VB, and
// zero chunks are refused; the 8-chunk mesh stays and is drawn as before.
C5R_TEST(huge_bad_meshes_keep_the_8_chunks) {
    Run a;
    if (!a.start(320, 180)) return;
    Mesh m = a.mesh;
    m.chunks.back().triangles += 1;   // 3 indices past the IB
    CHECK_ERR(a.r.setMesh(m), Err::BadLayout);
    m = a.mesh;
    m.chunks[3].baseVertex = static_cast<int32_t>(m.vb.size() / kIceStride);   // every index past the VB
    CHECK_ERR(a.r.setMesh(m), Err::BadLayout);
    m = a.mesh;
    m.chunks.clear();
    CHECK_ERR(a.r.setMesh(m), Err::BadLayout);
    CHECK_EQ(a.r.chunkCount(), kHugeChunks);
    if (!a.draw(a.in, nullptr)) return;
    CHECK_EQ(a.st.draws, kHugeChunks);
    checkRanges(a.hook.rec, a.mesh, 1);
}

C5R_TEST_MAIN
