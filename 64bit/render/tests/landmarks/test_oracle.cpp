// test_oracle.cpp - the model library against the EXE's own buffers (ticket 4872; landmark-renderer.md 4): for draws of
// 3910's captures (copied once from the exports to C5R_LANDMARK_SAMPLES, landmarks_testkit.h), the key of the captured
// index and vertex buffers finds the model file the index says the frame shows, and the library's repack equals the
// captured index range and the mesh's vertices byte for byte. Reads the install (C5R_GAME_DIR), the database copy and
// the samples at run time; a missing input skips. Prints counts and names only (D83).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "landmarks_testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

namespace {

struct Sample {
    const char* what;       // the object (3910 index.csv / 4870's notes)
    const char* ib;         // relative to C5R_LANDMARK_SAMPLES
    uint32_t ibytes;
    const char* vb;
    uint32_t stride;
    uint32_t first, count;
    int32_t base;
    const char* gr2;        // the file the key must name (among its candidates)
    float scale;
};

// W01: wonder frame (Colossus, a Euro Ancient block set, cotton, ruins, fish), Fanc01 object 9 (PL_MED iron heavy mine),
// Lanc07 (deer, superhorse, industrial fish). Draw numbers in landmark-renderer.md 4.1.
const Sample kSamples[] = {
    {"W01 d443 block set", "W01/buffers/5616_fcf4d5ea.bin", 2, "W01/buffers/5617_772824d8.bin", 32, 280, 105, 154,
     "euro_anc_city.gr2", 1.0f},
    {"W01 d156 Colossus", "W01/buffers/5792_287ece13.bin", 2, "W01/buffers/5793_b1fdae51.bin", 20, 1, 30771, 0,
     "colossus.gr2", 0.05f},
    {"W01 d528 cotton", "W01/buffers/40835_a530507a.bin", 2, "W01/buffers/40836_53e01ace.bin", 20, 481, 24, 180,
     "resource_cotton.gr2", 0.94f},
    {"W01 d99 ruins", "W01/buffers/39562_1baea5d1.bin", 2, "W01/buffers/39563_a69a9b11.bin", 20, 265, 180, 150,
     "ruins4.gr2", 1.0f},
    {"W01 d158 fish seagull", "W01/buffers/39872_78da7bc7.bin", 2, "W01/buffers/39873_b2af50e5.bin", 28, 511, 510, 156,
     "fish.gr2", 0.07f},
    {"Fanc01 d969 iron mine", "Fanc01/buffers/39720_7f70d117.bin", 2, "Fanc01/buffers/39721_7efbd827.bin", 20, 3817,
     255, 2569, "pl_med_mine_iron_heavy.gr2", 1.0f},
    {"Fanc01 d975 iron mine", "Fanc01/buffers/39720_7f70d117.bin", 2, "Fanc01/buffers/39721_7efbd827.bin", 20, 1315,
     1266, 876, "pl_med_mine_iron_heavy.gr2", 1.0f},
    {"Fanc01 d986 iron mine", "Fanc01/buffers/39720_7f70d117.bin", 2, "Fanc01/buffers/39721_7efbd827.bin", 20, 5533,
     144, 3138, "pl_med_mine_iron_heavy.gr2", 1.0f},
    {"Lanc07 d1007 deer", "Lanc07/buffers/39867_3fd57331.bin", 2, "Lanc07/buffers/39869_afc2856e.bin", 28, 1, 3108, 0,
     "resource_deer.gr2", 0.12f},
    {"Lanc07 d1134 superhorse", "Lanc07/buffers/39925_85daab6b.bin", 2, "Lanc07/buffers/39927_ef8328c4.bin", 28, 5503,
     2622, 711, "superhorse.gr2", 0.08f},
    {"Lanc07 d765 industrial fish", "Lanc07/buffers/39874_562e3ed1.bin", 2, "Lanc07/buffers/39875_dd253441.bin", 28,
     3076, 3330, 1213, "fish_industrial.gr2", 0.07f},
    // 4878 (city-renderer.md 12.4): files stored with 32-bit indices that the EXE draws R16.
    {"Lanc18 d799 embassy", "Lanc18/buffers/40500_4dfb2a79.bin", 2, "Lanc18/buffers/40501_129c5b6e.bin", 20, 1, 2739,
     0, "embassy.gr2", 1.2f},
    {"Lanc20 d722 Lalibela", "Lanc20/buffers/40638_5513b267.bin", 2, "Lanc20/buffers/40639_e1d10a27.bin", 20, 1, 366,
     0, "lalibela.gr2", 0.75f},
};

}  // namespace

C5R_TEST(captured_draws_are_found_and_byte_equal) {
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.artStatus);
    CHECK_OK(in.libStatus);
    if (!in.libStatus.ok()) return;
    const std::string dir = lmt::samplesDir();
    uint32_t equal = 0;
    for (const Sample& s : kSamples) {
        std::vector<uint8_t> ib, vb;
        if (!lmt::readAll(dir + "/" + s.ib, ib) || !lmt::readAll(dir + "/" + s.vb, vb))
            c5r::test::skip(std::string("capture sample not found under ") + dir + ": " + s.ib);
        MeshKey k;
        CHECK_OK(meshKey(ib.data(), ib.size() / s.ibytes, s.ibytes, kLeadIndices, vb.data(), vb.size(), s.first,
                         s.count, s.base, k));
        const std::vector<ModelLibrary::Hit> hits = in.lib.candidates(k);
        const ModelLibrary::Hit* hit = nullptr;
        for (const ModelLibrary::Hit& h : hits)
            if (in.lib.files()[h.file].gr2 == s.gr2 && in.lib.files()[h.file].scale == s.scale) hit = &h;
        std::printf("%s: %zu candidate(s)%s\n", s.what, hits.size(), hit ? "" : ", the expected file NOT among them");
        CHECK(hit != nullptr);
        if (!hit) continue;
        const ModelFile& f = in.lib.files()[hit->file];
        const Part& p = f.layout.parts[hit->part];
        CHECK(p.key == k);
        CHECK_EQ(f.layout.indexBytes, s.ibytes);
        CHECK_EQ(p.skinned ? 28u : vertexStride(f.layout.form), s.stride);
        const ModelLibrary::Buffers* b = nullptr;
        CHECK_OK(in.lib.resident(hit->file, &b));
        if (!b) continue;
        const std::vector<uint8_t>& ov = p.skinned ? b->skinnedVertices : b->vertices;
        // The whole buffers after the pad element, and the draw's own range and vertices.
        CHECK_EQ(b->indices.size(), ib.size());
        CHECK_EQ(ov.size(), vb.size());
        if (b->indices.size() != ib.size() || ov.size() != vb.size()) continue;
        const bool ibEq = std::memcmp(b->indices.data() + s.ibytes, ib.data() + s.ibytes, ib.size() - s.ibytes) == 0;
        const bool vbEq = std::memcmp(ov.data(), vb.data(), vb.size()) == 0;
        CHECK(ibEq);
        CHECK(vbEq);
        if (ibEq && vbEq) ++equal;
    }
    std::printf("byte-equal: %u of %zu sampled draws (index buffer after the pad and the whole vertex buffer)\n", equal,
                sizeof kSamples / sizeof kSamples[0]);
}

// Shared keys are told apart by the textures (landmark-renderer.md 2.4): the euro and medit Ancient sets share index and
// vertex buffers, the four PL_MED heavy mines too; every candidate of such a key has the same range and different t0.
C5R_TEST(shared_keys_differ_in_textures) {
    lmt::Install& in = lmt::install(true);
    if (!in.libStatus.ok()) {
        CHECK_OK(in.libStatus);
        return;
    }
    const int32_t euro = in.lib.fileIndex("euro_anc_city.gr2", 1.0f), medit = in.lib.fileIndex("medit_anc_city.gr2", 1.0f);
    CHECK(euro >= 0 && medit >= 0);
    if (euro < 0 || medit < 0) return;
    const ModelFile& e = in.lib.files()[static_cast<size_t>(euro)];
    const ModelFile& m = in.lib.files()[static_cast<size_t>(medit)];
    CHECK_EQ(e.layout.parts.size(), m.layout.parts.size());
    if (e.layout.parts.empty() || e.layout.parts.size() != m.layout.parts.size()) return;
    CHECK(e.layout.parts[0].key == m.layout.parts[0].key);
    CHECK(e.layout.parts[0].t0 != m.layout.parts[0].t0);
    const std::vector<ModelLibrary::Hit> c = in.lib.candidates(e.layout.parts[0].key);
    CHECK(c.size() >= 2u);
}

C5R_TEST_MAIN
