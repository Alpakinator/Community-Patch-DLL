// test_hostile.cpp - crafted files and structures that once crashed, hung or aborted the reader (bug 0093, defects
// V1-V4; issue 9025, 2026-11-11; V9-V11, issue 4000, 2026-11-13). Every crafted file is built here by tests/gr2_writer.h; nothing is read from disk,
// so this test never skips.
//
// Each case runs in its own child process (this executable with --case NAME) inside a job object with a memory cap
// and a time limit, so that a stack overflow, an access violation, an abort or a loop of 4.3e9 steps shows as that
// case failing with its exit code or "timeout" instead of taking the whole test down or hanging it. A case passes
// when the child ends with exit 0, i.e. the reader gave the named error (or result) the case expects.
#include "c5models/gr2.h"
#include "gr2_writer.h"
#include "testing.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace c5m;
using namespace gr2w;

namespace {

// ------------------------------------------------------------------ crafted files
// A chain of `depth` type definitions in one section, each with one Reference member whose type is the next
// definition (one-off 9021's tt_chain / chain_depth.py, rebuilt here). Root object: 16 zero bytes.
Bytes type_chain(uint32_t depth) {
    std::vector<Sec> s(1);
    Sec& t = s[0];
    for (uint32_t i = 0; i < depth; ++i) {
        uint32_t e = (uint32_t)t.b.size();
        t.b.resize(e + 64, 0);
        t.set32(e, 2);                                   // Reference, name slot NULL (inline strings: "")
        if (i + 1 < depth) t.ptr(e + 8, 0, e + 64);      // its type: the next definition
    }
    uint32_t root = (uint32_t)t.b.size();
    t.b.resize(root + 16, 0);
    return write_file(s, 0, root, 0, 0);
}

// Root {ArrayOfReferences Meshes -> Mesh}, Mesh {String Name; Reference PrimaryVertexData -> VD},
// VD {ReferenceToVariantArray Vertices}, vertex type V whose members are all Inline of an empty type E, so that
// V has 0 bytes but still names Position (and BoneWeights / BoneIndices when skinned). Vertex count `count`.
Bytes zero_stride(uint32_t count, bool skinned) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter E(t); uint32_t tE = E.end();
    TypeWriter V(t);
    V.member(1, "Position", tE);
    if (skinned) { V.member(1, "BoneWeights", tE); V.member(1, "BoneIndices", tE); }
    uint32_t tV = V.end();
    TypeWriter D(t); D.member(7, "Vertices"); uint32_t tD = D.end();
    TypeWriter M(t); M.member(8, "Name"); M.member(2, "PrimaryVertexData", tD); uint32_t tM = M.end();
    TypeWriter R(t); R.member(4, "Meshes", tM); uint32_t tR = R.end();
    finish_names(t, {&E, &V, &D, &M, &R});
    uint32_t root = o.put32(1); uint32_t mptr = o.put32(0);
    uint32_t marr = o.put32(0);
    uint32_t mesh = o.put32(0); uint32_t vdp = o.put32(0);
    uint32_t vd = o.put32(0); o.put32(count); uint32_t vptr = o.put32(0);
    uint32_t verts = o.put32(0);
    o.ptr(mptr, S_OBJ, marr);
    o.ptr(marr, S_OBJ, mesh);
    o.ptr(mesh, S_OBJ, o.cstr("ZeroStride"));
    o.ptr(vdp, S_OBJ, vd);
    o.ptr(vd, S_TYPE, tV);
    o.ptr(vptr, S_OBJ, verts);
    (void)root;
    return write_file(s, tR, 0);
}

// A valid small file (Root {String FromFileName}) whose sections are then re-declared as compressed.
Bytes small_file(uint32_t pad_obj = 0) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter R(t); R.member(8, "FromFileName"); uint32_t tR = R.end();
    finish_names(t, {&R});
    uint32_t root = o.put32(0);
    o.ptr(root, S_OBJ, o.cstr("small.max"));
    o.b.resize(o.b.size() + pad_obj, 0x5A);
    return write_file(s, tR, root);
}

// ------------------------------------------------------------------ crafted files of bug 0093 V9-V11 (issue 4000)
// Rebuilt from the verification rerun's own_cases.py (research/05-graphics-composition/evidence/4000-0093/), same
// shapes and sizes, so that the test needs no file from disk.

// A zero-filled block of n bytes at a 4-byte boundary of section s; returns its offset.
uint32_t alloc(Sec& s, size_t n) {
    s.align();
    uint32_t o = (uint32_t)s.b.size();
    s.b.resize(o + n, 0);
    return o;
}

// A type definition without member names, in section 0: (type, referenced definition in section 0 or ~0u, array).
struct RawMember { uint32_t type, ref, arr; };
uint32_t raw_type(Sec& s, const std::vector<RawMember>& ms) {
    uint32_t o = alloc(s, 32 * (ms.size() + 1));
    for (size_t i = 0; i < ms.size(); ++i) {
        s.set32(o + 32 * (uint32_t)i, ms[i].type);
        if (ms[i].ref != ~0u) s.ptr(o + 32 * (uint32_t)i + 8, 0, ms[i].ref);
        s.set32(o + 32 * (uint32_t)i + 12, ms[i].arr);
    }
    return o;
}

// One section holding types and objects; header 0x58 (string database CRC) set to sdb.
Bytes single_section(std::vector<Sec>& s, uint32_t root_type, uint32_t root_obj, uint32_t sdb) {
    Bytes b = write_file(s, root_type, root_obj, 0, S_OBJ);
    std::memcpy(&b[0x58], &sdb, 4);
    recrc(b);
    return b;
}

// V9: one mesh of 3 vertices of 18 B: Position Real32[3], BoneWeights UInt8[4] (64, 64, 64, 63), BoneIndices
// UInt8[2] (0, 1) last; 4 bone bindings. The first two bytes of vertices 1 and 2 are 200, so a reader that takes 4
// bone indices from the 2 there are reads 200 (past the 4 bindings) inside the buffer for vertices 0 and 1, and past
// the 54-byte buffer for vertex 2.
Bytes bone_indices_shorter() {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter V(t);
    V.member(10, "Position", ~0u, 3); V.member(12, "BoneWeights", ~0u, 4); V.member(12, "BoneIndices", ~0u, 2);
    uint32_t tV = V.end();
    TypeWriter BB(t); BB.member(8, "BoneName"); uint32_t tBB = BB.end();
    TypeWriter D(t); D.member(7, "Vertices"); uint32_t tD = D.end();
    TypeWriter M(t);
    M.member(8, "Name"); M.member(2, "PrimaryVertexData", tD); M.member(3, "BoneBindings", tBB);
    uint32_t tM = M.end();
    TypeWriter R(t); R.member(4, "Meshes", tM); uint32_t tR = R.end();
    finish_names(t, {&V, &BB, &D, &M, &R});
    uint32_t root = o.put32(1); uint32_t mptr = o.put32(0);
    uint32_t marr = o.put32(0);
    uint32_t mesh = o.put32(0); uint32_t vdp = o.put32(0); o.put32(4); uint32_t bbp = o.put32(0);
    uint32_t vd = o.put32(0); o.put32(3); uint32_t vptr = o.put32(0);
    uint32_t bb = alloc(o, 16);
    uint32_t verts = alloc(o, 3 * 18);
    for (uint32_t v = 0; v < 3; ++v) {
        uint8_t* p = &o.b[verts + 18 * v];
        if (v) p[0] = p[1] = 200;                // Position.x: a tiny, finite float
        p[12] = 64; p[13] = 64; p[14] = 64; p[15] = 63;
        p[16] = 0; p[17] = 1;
    }
    o.ptr(mesh, S_OBJ, o.cstr("V9"));
    for (uint32_t i = 0; i < 4; ++i) o.ptr(bb + 4 * i, S_OBJ, o.cstr("bone" + std::to_string(i)));
    o.ptr(mptr, S_OBJ, marr);
    o.ptr(marr, S_OBJ, mesh);
    o.ptr(vdp, S_OBJ, vd);
    o.ptr(bbp, S_OBJ, bb);
    o.ptr(vd, S_TYPE, tV);
    o.ptr(vptr, S_OBJ, verts);
    (void)root;
    return write_file(s, tR, 0);
}

// V10: Meshes = `refs` references to one mesh of `vcount` vertices (Position Real32[3]), all zero.
Bytes shared_mesh(uint32_t refs, uint32_t vcount) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter V(t); V.member(10, "Position", ~0u, 3); uint32_t tV = V.end();
    TypeWriter D(t); D.member(7, "Vertices"); uint32_t tD = D.end();
    TypeWriter M(t); M.member(8, "Name"); M.member(2, "PrimaryVertexData", tD); uint32_t tM = M.end();
    TypeWriter R(t); R.member(4, "Meshes", tM); uint32_t tR = R.end();
    finish_names(t, {&V, &D, &M, &R});
    uint32_t root = o.put32(refs); uint32_t sptr = o.put32(0);
    uint32_t mesh = o.put32(0); uint32_t vdp = o.put32(0);
    uint32_t vd = o.put32(0); o.put32(vcount); uint32_t vptr = o.put32(0);
    uint32_t slots = alloc(o, 4ull * refs);
    uint32_t verts = alloc(o, 12ull * vcount);
    for (uint32_t i = 0; i < refs; ++i) o.ptr(slots + 4 * i, S_OBJ, mesh);
    o.ptr(sptr, S_OBJ, slots);
    o.ptr(mesh, S_OBJ, o.cstr("shared"));
    o.ptr(vdp, S_OBJ, vd);
    o.ptr(vd, S_TYPE, tV);
    o.ptr(vptr, S_OBJ, verts);
    (void)root;
    return write_file(s, tR, 0);
}

// V10: k one-String types, each walked over one shared array of n 4-byte elements (string database CRC set, so the
// walk over every object runs): k x n distinct (address, type) visits.
Bytes walk_types(uint32_t k, uint32_t n) {
    std::vector<Sec> s(1);
    Sec& x = s[0];
    std::vector<RawMember> rm;
    for (uint32_t i = 0; i < k; ++i) rm.push_back(RawMember{3, raw_type(x, {RawMember{8, ~0u, 0}}), 0});
    uint32_t rt = raw_type(x, rm);
    uint32_t arr = alloc(x, 4ull * n);
    uint32_t root = alloc(x, 8ull * k);
    for (uint32_t i = 0; i < k; ++i) {
        x.set32(root + 8 * i, n);
        x.ptr(root + 8 * i + 4, 0, arr);
    }
    return single_section(s, rt, root, 0x12345678);
}

// V10/V11: one run of m String members; its m suffixes are m overlapping type definitions (m .. 1 members), so the
// reader once built m(m+1)/2 member records. The root type references the m suffixes: Reference members when
// nbytes == 0, else ReferenceToArray members whose arrays all lie over one shared array of nbytes (the walk then
// reads about m x nbytes / 4 string slots).
Bytes suffix_types(uint32_t m, uint32_t nbytes, uint32_t sdb) {
    std::vector<Sec> s(1);
    Sec& x = s[0];
    uint32_t arr = nbytes ? alloc(x, nbytes) : 0;
    uint32_t run = alloc(x, 32ull * (m + 1));
    for (uint32_t j = 0; j < m; ++j) x.set32(run + 32 * j, 8);
    std::vector<RawMember> rm;
    for (uint32_t j = 0; j < m; ++j) rm.push_back(RawMember{nbytes ? 3u : 2u, run + 32 * j, 0});
    uint32_t rt = raw_type(x, rm);
    uint32_t ro = alloc(x, (nbytes ? 8ull : 4ull) * m);
    if (nbytes)
        for (uint32_t j = 0; j < m; ++j) {
            x.set32(ro + 8 * j, nbytes / (4 * (m - j)));
            x.ptr(ro + 8 * j + 4, 0, arr);
        }
    return single_section(s, rt, ro, sdb);
}

// V11: a string database {Strings: ReferenceToArray of n {String}, DatabaseCRC} whose n slots all point at one string
// of len bytes: the reader once copied and hashed it n times (64 GiB for 65,536 x 1 MiB).
Bytes gsd_long_string(uint32_t n, uint32_t len) {
    std::vector<Sec> s(2);
    Sec& o = s[S_OBJ];
    Sec& t = s[S_TYPE];
    TypeWriter E(t); E.member(8, "String"); uint32_t tE = E.end();
    TypeWriter R(t); R.member(3, "Strings", tE); R.member(20, "DatabaseCRC"); uint32_t tR = R.end();
    finish_names(t, {&E, &R});
    uint32_t root = o.put32(n); uint32_t rp = o.put32(0); o.put32(0x1234);
    uint32_t st = (uint32_t)o.b.size();
    o.b.resize(st + (size_t)len, (uint8_t)'A');
    o.b.push_back(0);
    uint32_t arr = alloc(o, 4ull * n);
    for (uint32_t i = 0; i < n; ++i) o.ptr(arr + 4 * i, S_OBJ, st);
    o.ptr(rp, S_OBJ, arr);
    (void)root;
    return write_file(s, tR, 0);
}

// ------------------------------------------------------------------ one case (child process)
int expect(const char* name, bool ok, const Error& e, Err want, const char* text) {
    bool pass = !ok && e.code == want && e.what.find(text) != std::string::npos;
    std::printf("  case %s: %s%s%s -> %s\n", name, ok ? "read OK" : err_name(e.code), ok ? "" : ": ",
                ok ? "" : e.what.c_str(), pass ? "as expected" : "NOT AS EXPECTED");
    return pass ? 0 : 1;
}

// read, then the checks and the summary as gr2sweep runs them (each could crash or loop on a bad structure)
bool read_all(const Bytes& b, Error& e) {
    FileInfo fi;
    if (!read_gr2(b.data(), b.size(), nullptr, fi, &e)) return false;
    GeometryReport rep;
    Error ge;
    check_geometry(fi, rep, &ge);
    canonical_summary(fi, "crafted");
    return true;
}

int run_case(const std::string& c) {
    Error e;
    if (c == "v1_chain_5000") {           // V1: stack overflow 0xC00000FD before the fix (1,500 was enough)
        Bytes b = type_chain(5000);
        return expect(c.c_str(), read_all(b, e), e, Err::TypeTreeCorrupt, "nested more than");
    }
    if (c == "v1_chain_at_limit") {       // the limit itself: a chain of exactly kMaxTypeNesting still reads
        Bytes b = type_chain(kMaxTypeNesting);
        bool ok = read_all(b, e);
        std::printf("  case %s: chain of %u: %s %s\n", c.c_str(), kMaxTypeNesting, ok ? "read OK" : err_name(e.code),
                    e.what.c_str());
        return ok ? 0 : 1;
    }
    if (c == "v1_chain_over_limit") {
        Bytes b = type_chain(kMaxTypeNesting + 1);
        return expect(c.c_str(), read_all(b, e), e, Err::TypeTreeCorrupt, "nested more than");
    }
    if (c == "v2_zero_stride_skinned") {  // V2: access violation in check_geometry before the fix
        Bytes b = zero_stride(3, true);
        return expect(c.c_str(), read_all(b, e), e, Err::ObjectOutOfRange, "array of 0-byte elements");
    }
    if (c == "v2_zero_stride_count_max") {   // V2: about 4.3e9 empty steps (no answer in 60 s) before the fix
        Bytes b = zero_stride(0xFFFFFFFFu, false);
        return expect(c.c_str(), read_all(b, e), e, Err::ObjectOutOfRange, "array of 0-byte elements");
    }
    if (c == "v2_check_geometry_zero_stride") {   // V2 through the API: a FileInfo whose weights lie outside the vertex
        FileInfo fi;
        Mesh m;
        m.name = "hand";
        m.vertex_count = 3;
        m.format.stride = 0;
        VertexComponent w; w.type = MemberType::NormalUInt8; w.name = "BoneWeights"; w.count = 4;
        VertexComponent ix; ix.type = MemberType::UInt8; ix.name = "BoneIndices"; ix.count = 4;
        m.format.components = {w, ix};
        fi.meshes.push_back(m);
        GeometryReport rep;
        Error ge;
        bool ok = check_geometry(fi, rep, &ge);
        return expect(c.c_str(), ok, ge, Err::GeometryCheck, "BoneWeights outside the vertex");
    }
    if (c == "v3_short_vertex_bytes") {   // V3: reads every vertex after reporting the size mismatch (null read here)
        FileInfo fi;
        Mesh m;
        m.name = "hand";
        m.vertex_count = 1000;
        m.format.stride = 12;
        VertexComponent p; p.type = MemberType::Real32; p.name = "Position"; p.count = 3;
        m.format.components = {p};
        fi.meshes.push_back(m);   // no vertex bytes at all
        GeometryReport rep;
        Error ge;
        bool ok = check_geometry(fi, rep, &ge);
        float out[4];
        uint32_t n = fi.meshes[0].read(0, "Position", out);   // Mesh::read itself refuses to read past the buffer
        std::string s = canonical_summary(fi, "hand");
        std::printf("  Mesh::read past the buffer -> %u components; %zu violation(s)\n", n, rep.violations.size());
        int r = expect(c.c_str(), ok, ge, Err::GeometryCheck, "vertex bytes 0 != count x stride");
        return (r == 0 && n == 0 && rep.violations.size() == 1) ? 0 : 1;
    }
    if (c == "v4_amplification") {        // V4: 2 sections stored empty, each declared 1 GiB; abort 0xC0000409 before
        Bytes b = small_file();
        for (uint32_t i = 0; i < 2; ++i) {
            set_section_field(b, i, 0, 2);
            set_section_field(b, i, 2, 0);
            set_section_field(b, i, 3, 1u << 30);
        }
        recrc(b);
        int r = expect(c.c_str(), read_all(b, e), e, Err::BadSectionTable, "more than 256 times");
        StringDatabase db;
        Error de;
        bool dok = db.add_gsd(b.data(), b.size(), &de);   // the string database's load has the same path
        return r | expect("v4_amplification (as .gsd)", dok, de, Err::BadSectionTable, "more than 256 times");
    }
    if (c == "v4_total_budget") {         // the total limit: 2 x 150 MiB within the ratio, over 256 MiB in all
        Bytes b = small_file(620 * 1024);
        uint32_t stored;
        std::memcpy(&stored, &b[0x68 + 8], 4);
        for (uint32_t i = 0; i < 2; ++i) {
            set_section_field(b, i, 0, 2);
            set_section_field(b, i, 2, stored);   // both point at section 0's bytes (inside the file)
            set_section_field(b, i, 1, 0x68 + 44 * 2);
            set_section_field(b, i, 3, 150u << 20);
        }
        recrc(b);
        return expect(c.c_str(), read_all(b, e), e, Err::BadSectionTable, "in all");
    }
    if (c == "v4_out_of_memory") {        // inside the limits, more than the child may commit (cap 128 MiB)
        Bytes b = small_file(1024 * 1024);
        uint32_t stored;
        std::memcpy(&stored, &b[0x68 + 8], 4);
        set_section_field(b, 0, 0, 2);
        set_section_field(b, 0, 3, 200u << 20);   // 200 MiB from about 1 MiB: ratio about 200
        recrc(b);
        return expect(c.c_str(), read_all(b, e), e, Err::OutOfMemory, "out of memory");
    }
    if (c == "v9_bone_indices_shorter") {   // V9: ASan heap-buffer-overflow READ 1 in check_geometry before the fix
        Bytes b = bone_indices_shorter();
        FileInfo fi;
        bool ok = read_gr2(b.data(), b.size(), nullptr, fi, &e);
        GeometryReport rep;
        Error ge;
        bool gok = ok && check_geometry(fi, rep, &ge);
        std::printf("  case %s: %s%s; check_geometry %s%s -> %s\n", c.c_str(), ok ? "read OK" : err_name(e.code),
                    ok ? "" : (": " + e.what).c_str(), gok ? "clean" : "violation: ", gok ? "" : ge.what.c_str(),
                    gok ? "as expected" : "NOT AS EXPECTED");
        return gok && fi.meshes.size() == 1 && fi.meshes[0].bone_bindings.size() == 4 ? 0 : 1;
    }
    if (c == "v10_shared_mesh_4096_refs") {   // V10: asked for 4 GiB (1 GiB cap: OutOfMemory) before the fix
        Bytes b = shared_mesh(4096, 87381);
        return expect(c.c_str(), read_all(b, e), e, Err::ReadBudget, "kMaxBuiltBytes");
    }
    if (c == "v10_shared_mesh_64_refs") {     // the same shape at 64 references (64 MiB of copies) still reads
        Bytes b = shared_mesh(64, 87381);
        FileInfo fi;
        bool ok = read_gr2(b.data(), b.size(), nullptr, fi, &e);
        std::printf("  case %s: %s %s; built %llu B, %llu steps\n", c.c_str(), ok ? "read OK" : err_name(e.code),
                    e.what.c_str(), (unsigned long long)fi.stats.built_bytes, (unsigned long long)fi.stats.read_steps);
        return ok && fi.meshes.size() == 64 ? 0 : 1;
    }
    if (c == "v10_suffix_types_1x4096") {     // V10: 8.4 million member records, 643 MB from 328 KB before the fix
        Bytes b = suffix_types(4096, 0, 0);
        return expect(c.c_str(), read_all(b, e), e, Err::ReadBudget, "read budget");
    }
    if (c == "v10_walk_types_4096_x_65536") { // V10: 1 GiB cap, then 0xC00000FD, before the fix
        Bytes b = walk_types(4096, 65536);
        return expect(c.c_str(), read_all(b, e), e, Err::ReadBudget, "read budget");
    }
    if (c == "v11_walk_suffix_1024_x_4096K") {   // V11: 358 s before the fix
        Bytes b = suffix_types(1024, 4u << 20, 0x12345678);
        return expect(c.c_str(), read_all(b, e), e, Err::ReadBudget, "kMaxReadSteps");
    }
    if (c == "v11_gsd_long_string_65536x1M") {   // V11: 219 s before the fix (add_gsd, as gr2sweep --gsd loads it)
        Bytes b = gsd_long_string(65536, 1u << 20);
        StringDatabase db;
        return expect(c.c_str(), db.add_gsd(b.data(), b.size(), &e), e, Err::ReadBudget, "kMaxBuiltBytes");
    }
    std::printf("unknown case %s\n", c.c_str());
    return 2;
}

// ------------------------------------------------------------------ the parent: one capped child per case
struct Case {
    const char* name;
    SIZE_T mem_cap;   // job memory limit per process
};

int run_child(const Case& c, DWORD timeout_ms) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --case ";
    for (const char* p = c.name; *p; ++p) cmd += (wchar_t)*p;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
    li.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    li.ProcessMemoryLimit = c.mem_cap;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    // the child writes into this process's stdout/stderr (no console of its own: CREATE_NO_WINDOW)
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    for (HANDLE h : {si.hStdOutput, si.hStdError})
        if (h && h != INVALID_HANDLE_VALUE) SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    PROCESS_INFORMATION pi = {};
    std::fflush(stdout);
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        std::printf("FAIL case %s: CreateProcess error %lu\n", c.name, GetLastError());
        CloseHandle(job);
        return -1;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ULONGLONG t0 = GetTickCount64();
    ResumeThread(pi.hThread);
    DWORD w = WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 0;
    if (w == WAIT_TIMEOUT) {
        TerminateJobObject(job, 0xDEAD);
        WaitForSingleObject(pi.hProcess, 5000);
        std::printf("FAIL case %s: timeout after %lu ms (killed)\n", c.name, timeout_ms);
        code = 0xFFFFFFFF;
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
        if (code) std::printf("FAIL case %s: child exit 0x%08lX\n", c.name, code);
    }
    // progress line (issue 4000): time and peak commit of every case against its cap
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION q = {};
    QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &q, sizeof q, nullptr);
    std::printf("[case %s] exit 0x%08lX, %.1f s, peak %.0f MB (cap %.0f MB)\n", c.name, code,
                (GetTickCount64() - t0) / 1000.0, q.PeakProcessMemoryUsed / 1048576.0, c.mem_cap / 1048576.0);
    std::fflush(stdout);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return (int)code;
}

}  // namespace

int main(int argc, char** argv) {
    // No error dialog or crash-report window from a failing child: the error mode is inherited by the children,
    // abort() exits with code 3 without the fault report, and the job ends a child on an unhandled exception.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc == 3 && std::strcmp(argv[1], "--case") == 0) {
        int r = run_case(argv[2]);
        std::fflush(stdout);
        return r;
    }
    const SIZE_T MiB = 1024 * 1024;
    Case cases[] = {
        {"v1_chain_5000", 512 * MiB},
        {"v1_chain_at_limit", 512 * MiB},
        {"v1_chain_over_limit", 512 * MiB},
        {"v2_zero_stride_skinned", 512 * MiB},
        {"v2_zero_stride_count_max", 512 * MiB},
        {"v2_check_geometry_zero_stride", 512 * MiB},
        {"v3_short_vertex_bytes", 512 * MiB},
        {"v4_amplification", 512 * MiB},
        {"v4_total_budget", 512 * MiB},
        {"v9_bone_indices_shorter", 512 * MiB},
        {"v10_shared_mesh_4096_refs", 1024 * MiB},
        {"v10_shared_mesh_64_refs", 1024 * MiB},
        {"v10_suffix_types_1x4096", 1024 * MiB},
        {"v10_walk_types_4096_x_65536", 1024 * MiB},
        {"v11_walk_suffix_1024_x_4096K", 1024 * MiB},
        {"v11_gsd_long_string_65536x1M", 1024 * MiB},
#if !defined(__SANITIZE_ADDRESS__)
        // AddressSanitizer's operator new ends the process on a failed allocation instead of throwing
        // std::bad_alloc, so this case exists only in builds without it
        {"v4_out_of_memory", 128 * MiB},
#endif
    };
    int n = 0;
    for (const Case& c : cases) {
        ++n;
        CHECK_EQ(run_child(c, 20000), 0);
    }
#if defined(__SANITIZE_ADDRESS__)
    std::printf("AddressSanitizer build: %d cases (v4_out_of_memory needs a throwing operator new; not built)\n", n);
#else
    std::printf("%d cases\n", n);
#endif
    return t::finish();
}
