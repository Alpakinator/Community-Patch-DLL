// test_unit_programs_table.cpp - issue 4751 (epic 0040): shell/common/unit_programs.h, the compositor's unit-draw
// filter table, equals the server's program table (render/src/units/programs.cpp unitProgramTable()): the DROP programs
// and pairs are exactly its pass-0 (Scene) rows, the KEEP shadow-mask programs exactly its ShadowMask rows; the two
// selection-effect programs are in neither the server's table nor DROP. A mutated copy of either table is caught
// (negative cases). No game data.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "../../../shell/common/unit_programs.h"
#include "c5r/units/draw.h"
#include "testkit.h"

using namespace c5r::units;

namespace {

std::string hex(const uint8_t* d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i) {
        s.push_back(x[d[i] >> 4]);
        s.push_back(x[d[i] & 15]);
    }
    return s;
}

uint8_t matBit(Material m) {
    switch (m) {
    case Material::Static: return VPUNIT_MAT87;
    case Material::SkinnedAlpha: return VPUNIT_MAT89;
    case Material::Skinned: return VPUNIT_MAT90;
    }
    return 0;
}

// (digest, stage, materials, va) of the server's rows of one pass, programs merged by digest and stage.
typedef std::tuple<std::string, uint8_t, uint8_t, uint32_t> Prog;
std::set<Prog> serverPrograms(PassKind pass) {
    std::vector<Prog> v;
    for (auto& r : unitProgramTable()) {
        if (r.pass != pass) continue;
        const uint8_t stage = r.vertex ? VPUNIT_STAGE_VS : VPUNIT_STAGE_PS;
        bool merged = false;
        for (auto& p : v)
            if (std::get<0>(p) == r.sha256 && std::get<1>(p) == stage) {
                std::get<2>(p) = static_cast<uint8_t>(std::get<2>(p) | matBit(r.material));
                merged = true;
            }
        if (!merged) v.emplace_back(r.sha256, stage, matBit(r.material), r.va);
    }
    return std::set<Prog>(v.begin(), v.end());
}

std::set<Prog> shellPrograms(const VPUnitProgram* t, int n, uint8_t pass) {
    std::set<Prog> s;
    for (int i = 0; i < n; ++i)
        if (t[i].pass == pass) s.emplace(hex(t[i].sha256), t[i].stage, t[i].materials, t[i].exeVa);
    return s;
}

// (material, VS digest, PS digest) of the server's Scene rows.
std::set<std::tuple<int, std::string, std::string>> serverPairs() {
    std::set<std::tuple<int, std::string, std::string>> s;
    for (auto& r : unitProgramTable()) {
        if (r.pass != PassKind::Scene || !r.vertex) continue;
        for (auto& q : unitProgramTable())
            if (q.pass == PassKind::Scene && !q.vertex && q.material == r.material)
                s.emplace(static_cast<int>(r.material), r.sha256, q.sha256);
    }
    return s;
}

std::set<std::tuple<int, std::string, std::string>> shellPairs(const VPUnitProgram* t, int nt, const VPUnitPair* p, int np) {
    std::set<std::tuple<int, std::string, std::string>> s;
    for (int i = 0; i < np; ++i) {
        if (p[i].vs >= nt || p[i].ps >= nt) {
            s.emplace(-1, "index out of range", "");
            continue;
        }
        s.emplace(p[i].material, hex(t[p[i].vs].sha256), hex(t[p[i].ps].sha256));
    }
    return s;
}

// The comparisons the cases below run, on any copy of the tables (so a mutated copy can be checked to fail).
std::string compare(const VPUnitProgram* drop, int nd, const VPUnitPair* pairs, int np, const VPUnitProgram* keep, int nk) {
    std::string why;
    if (shellPrograms(drop, nd, 0) != serverPrograms(PassKind::Scene)) why += "DROP programs differ from the Scene rows; ";
    if (shellPrograms(drop, nd, 1).size() || shellPrograms(drop, nd, 0xFF).size()) why += "DROP holds a program not of pass 0; ";
    if (shellPairs(drop, nd, pairs, np) != serverPairs()) why += "DROP pairs differ from the Scene rows; ";
    for (int i = 0; i < np; ++i) {
        if (pairs[i].vs >= nd || pairs[i].ps >= nd) {
            why += "a pair's index is past the table; ";
            continue;
        }
        if (drop[pairs[i].vs].stage != VPUNIT_STAGE_VS || drop[pairs[i].ps].stage != VPUNIT_STAGE_PS) why += "a pair's stages are wrong; ";
    }
    if (shellPrograms(keep, nk, 1) != serverPrograms(PassKind::ShadowMask)) why += "KEEP mask programs differ from the ShadowMask rows; ";
    std::set<std::string> server, dropDigests;
    for (auto& r : unitProgramTable()) server.insert(r.sha256);
    for (int i = 0; i < nd; ++i) dropDigests.insert(hex(drop[i].sha256));
    for (int i = 0; i < nk; ++i) {
        if (keep[i].pass == 0) why += "KEEP holds a pass-0 program; ";
        if (dropDigests.count(hex(keep[i].sha256))) why += "a KEEP program is in DROP; ";
        if (keep[i].pass == 0xFF && server.count(hex(keep[i].sha256))) why += "a selection program is in the server's table; ";
    }
    return why;
}

// ticket 4785: (material, VS digest, PS digest) of the server's ShadowMask rows, and the shell's mask pairs (indices into KEEP)
std::set<std::tuple<int, std::string, std::string>> serverMaskPairs() {
    std::set<std::tuple<int, std::string, std::string>> s;
    for (auto& r : unitProgramTable()) {
        if (r.pass != PassKind::ShadowMask || !r.vertex) continue;
        for (auto& q : unitProgramTable())
            if (q.pass == PassKind::ShadowMask && !q.vertex && q.material == r.material)
                s.emplace(static_cast<int>(r.material), r.sha256, q.sha256);
    }
    return s;
}

std::string compareMask(const VPUnitProgram* keep, int nk, const VPUnitPair* pairs, int np) {
    std::string why;
    if (shellPairs(keep, nk, pairs, np) != serverMaskPairs()) why += "mask pairs differ from the ShadowMask rows; ";
    for (int i = 0; i < np; ++i) {
        if (pairs[i].vs >= nk || pairs[i].ps >= nk) continue;   // named above ("index out of range")
        if (keep[pairs[i].vs].stage != VPUNIT_STAGE_VS || keep[pairs[i].ps].stage != VPUNIT_STAGE_PS) why += "a mask pair's stages are wrong; ";
        if (keep[pairs[i].vs].pass != 1 || keep[pairs[i].ps].pass != 1) why += "a mask pair names a program not of pass 1; ";
    }
    return why;
}

C5R_TEST(mask_pairs_equal_the_server_shadow_mask_rows) {
    const std::string why = compareMask(kVPUnitKeep, VPUNIT_KEEP_PROGRAMS, kVPUnitMaskPairs, VPUNIT_MASK_PAIRS);
    if (!why.empty()) std::printf("  mismatch: %s\n", why.c_str());
    CHECK(why.empty());
    CHECK_EQ(serverMaskPairs().size(), static_cast<size_t>(VPUNIT_MASK_PAIRS));
    // a mutated copy is caught: a pair pointing at the selection effect, a swapped material, an index past the table
    VPUnitPair p[VPUNIT_MASK_PAIRS];
    std::memcpy(p, kVPUnitMaskPairs, sizeof p);
    p[0].ps = 6;
    CHECK(!compareMask(kVPUnitKeep, VPUNIT_KEEP_PROGRAMS, p, VPUNIT_MASK_PAIRS).empty());
    std::memcpy(p, kVPUnitMaskPairs, sizeof p);
    p[1].material = 90;
    CHECK(!compareMask(kVPUnitKeep, VPUNIT_KEEP_PROGRAMS, p, VPUNIT_MASK_PAIRS).empty());
    std::memcpy(p, kVPUnitMaskPairs, sizeof p);
    p[2].vs = 40;
    CHECK(!compareMask(kVPUnitKeep, VPUNIT_KEEP_PROGRAMS, p, VPUNIT_MASK_PAIRS).empty());
}

C5R_TEST(tables_equal_the_server_program_table) {
    const std::string why = compare(kVPUnitDrop, VPUNIT_DROP_PROGRAMS, kVPUnitDropPairs, VPUNIT_DROP_PAIRS, kVPUnitKeep,
                                    VPUNIT_KEEP_PROGRAMS);
    if (!why.empty()) std::printf("  mismatch: %s\n", why.c_str());
    CHECK(why.empty());
    CHECK_EQ(serverPrograms(PassKind::Scene).size(), static_cast<size_t>(VPUNIT_DROP_PROGRAMS));
    CHECK_EQ(serverPairs().size(), static_cast<size_t>(VPUNIT_DROP_PAIRS));
}

C5R_TEST(drop_holds_the_three_unit_pixel_shaders) {
    std::set<std::string> ps;
    for (int i = 0; i < VPUNIT_DROP_PROGRAMS; ++i)
        if (kVPUnitDrop[i].stage == VPUNIT_STAGE_PS) ps.insert(hex(kVPUnitDrop[i].sha256).substr(0, 8));
    CHECK(ps == (std::set<std::string>{"60d270e1", "5def4f99", "0e7a3566"}));
    // the selection effect (VS da2382af / PS 8d853988, breakdown O4) is recognised and kept
    std::set<std::string> sel;
    for (int i = 0; i < VPUNIT_KEEP_PROGRAMS; ++i)
        if (kVPUnitKeep[i].pass == 0xFF) sel.insert(hex(kVPUnitKeep[i].sha256).substr(0, 8));
    CHECK(sel == (std::set<std::string>{"da2382af", "8d853988"}));
}

// Negative cases: each mutation of a copy must make compare() report a difference.
C5R_TEST(a_mutated_copy_is_caught) {
    VPUnitProgram d[VPUNIT_DROP_PROGRAMS], k[VPUNIT_KEEP_PROGRAMS];
    VPUnitPair p[VPUNIT_DROP_PAIRS];
    auto reset = [&]() {
        std::memcpy(d, kVPUnitDrop, sizeof d);
        std::memcpy(k, kVPUnitKeep, sizeof k);
        std::memcpy(p, kVPUnitDropPairs, sizeof p);
    };
    reset();
    CHECK(compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    for (int i = 0; i < VPUNIT_DROP_PROGRAMS; ++i) {   // one digest byte of each DROP program
        reset();
        d[i].sha256[31] ^= 1;
        CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    }
    for (int i = 0; i < VPUNIT_KEEP_PROGRAMS; ++i) {   // one digest byte of each mask program; a selection program
        reset();                                        // moved into the pass-0 set
        if (k[i].pass == 1) k[i].sha256[0] ^= 0x80;
        else k[i].pass = 0;
        CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    }
    reset();
    p[0].ps = 3;   // material 90 paired with 89's PS
    CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    reset();
    p[2].vs = 9;   // index past the table
    CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    reset();
    d[2].materials = VPUNIT_MAT89;   // wrong material bits
    CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    reset();
    d[4].exeVa ^= 4;   // wrong VA
    CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
    reset();   // a mask program moved into DROP
    d[0] = kVPUnitKeep[0];
    CHECK(!compare(d, VPUNIT_DROP_PROGRAMS, p, VPUNIT_DROP_PAIRS, k, VPUNIT_KEEP_PROGRAMS).empty());
}

}  // namespace

C5R_TEST_MAIN
