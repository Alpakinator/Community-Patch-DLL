// test_library.cpp - c5r::landmarks::ModelLibrary over the install (ticket 4872; landmark-renderer.md 3): the counts
// against cities-and-landmarks.md / landmarks-and-models.md, the files refused and why, every key found again, the
// identity sets per class, lazy residency and its memory count, and the load's refusals. Reads the install
// (C5R_GAME_DIR) and the database copy (landmarks_testkit.h) at run time; a missing input skips.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "landmarks_testkit.h"

using namespace c5r;
using namespace c5r::landmarks;

C5R_TEST(counts_against_the_knowledge_base) {
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.artStatus);
    CHECK_OK(in.libStatus);
    if (!in.libStatus.ok()) return;
    const LibraryCounts& c = in.lib.counts();
    std::printf("rows %u (%u with a model, %u names), wonder types %u (%u models, %u drawn), files %u (%u ok), parts %u, "
                "keys %u, collisions %u (%u textured), catalogue %.0f KiB, load %.2f s\n",
                c.landmarkRows, c.landmarkRowsWithModel, c.landmarkModels, c.wonderTypes, c.wonderModels,
                c.wonderTypesDrawn, c.files, c.filesOk, c.parts, c.keys, c.keyCollisions, c.keyCollisionsTextured,
                c.catalogueBytes / 1024.0, c.loadSeconds);
    CHECK_EQ(c.landmarkRows, 667u);              // landmarks-and-models.md 1.2
    CHECK_EQ(c.landmarkRowsWithModel, 659u);
    CHECK_EQ(c.landmarkModels, 508u);            // distinct .fxsxml base names (514 distinct Model paths)
    CHECK_EQ(c.wonderTypes, 76u);                // model-reader.md 3
    CHECK_EQ(c.wallFiles, 7u);
    CHECK_EQ(c.greatWallFiles, 2u);
    CHECK_EQ(c.citySetEntries, 28u);             // 7 cultures x 4 slots
    CHECK_EQ(c.citySetFiles, 25u);               // City_Ruins.gr2 for all four Ruins slots
    CHECK_EQ(c.filesOk + c.filesFailed, c.files);
    // The files refused on this install, each for its reason (landmark-renderer.md 3.2).
    std::set<std::string> failed;
    for (const ModelFile& f : in.lib.files())
        if (!f.ok) {
            failed.insert(f.gr2);
            std::printf("refused: %s: %s\n", f.gr2.c_str(), f.problem.c_str());
            CHECK(!f.problem.empty());
            CHECK(f.layout.parts.empty());
        }
    CHECK(failed == std::set<std::string>({"oil1.gr2", "oil_heavy2.gr2", "schloss_hb.gr2"}));
    for (const Model& m : in.lib.models()) CHECK(m.file >= 0);
    // Every wonder drawn by a display building has a model; every model of a class has that class's files.
    for (const Model& m : in.lib.models()) {
        if (m.file < 0) continue;
        CHECK(in.lib.files()[static_cast<size_t>(m.file)].cls == m.cls);
    }
    const Model* colossus = in.lib.model("colossus.fxsxml");
    CHECK(colossus != nullptr);
    if (colossus) {
        CHECK(colossus->source == Source::Wonder);
        CHECK_EQ(colossus->scale, 0.05f);
        CHECK(colossus->cls == ModelClass::CityPiece);
    }
    const Model* deer = in.lib.model("Resource_Deer.fxsxml");
    CHECK(deer != nullptr);
    if (deer) {
        CHECK(deer->animated);
        CHECK(deer->source == Source::LandmarkRow);
        CHECK(!deer->rows.empty());
        const ModelFile& f = in.lib.files()[static_cast<size_t>(deer->file)];
        CHECK(f.origin.find("Expansion2Models.fpk") != std::string::npos);   // the copy the EXE draws (#6025)
        CHECK(f.layout.skinnedVertexCount > 0);
    }
    const Model* block = in.lib.model("euro_anc_city.gr2");
    CHECK(block != nullptr);
    if (block) CHECK(in.lib.files()[static_cast<size_t>(block->file)].layout.form == VertexForm::Float32);
    for (const LandmarkRow& r : in.lib.landmarkRows()) {
        CHECK(r.row.model_null == (r.model < 0));
        if (r.row.layout == "RANDOM") CHECK(r.handler == Handler::Random);
    }
}

C5R_TEST(every_key_is_found_and_published_by_class) {
    lmt::Install& in = lmt::install(true);
    if (!in.libStatus.ok()) {
        CHECK_OK(in.libStatus);
        return;
    }
    uint32_t parts = 0;
    for (uint32_t fi = 0; fi < in.lib.files().size(); ++fi)
        for (uint32_t pi = 0; pi < in.lib.files()[fi].layout.parts.size(); ++pi) {
            const MeshKey& k = in.lib.files()[fi].layout.parts[pi].key;
            const ModelLibrary::Hit* h = in.lib.find(k);
            CHECK(h != nullptr);
            bool listed = false;
            for (const ModelLibrary::Hit& c : in.lib.candidates(k)) listed = listed || (c.file == fi && c.part == pi);
            CHECK(listed);
            ++parts;
        }
    CHECK_EQ(parts, in.lib.counts().parts);
    size_t total = 0;
    for (ModelClass c : {ModelClass::CityBlock, ModelClass::CityPiece, ModelClass::Landmark}) {
        const std::vector<MeshKey> v = in.lib.keys(c);
        std::printf("%s: %zu keys\n", className(c), v.size());
        CHECK(!v.empty());
        CHECK(std::is_sorted(v.begin(), v.end()));
        total += v.size();
    }
    CHECK(in.lib.keys(ModelClass::Unknown).empty());
    CHECK_EQ(total, static_cast<size_t>(in.lib.counts().keys));
    MeshKey none;
    none.indexHash = 1;
    CHECK(in.lib.find(none) == nullptr);
    CHECK(in.lib.candidates(none).empty());
}

C5R_TEST(residency_is_lazy_and_counted) {
    lmt::Install& in = lmt::install(true);
    if (!in.libStatus.ok()) {
        CHECK_OK(in.libStatus);
        return;
    }
    ModelLibrary& lib = in.lib;
    lib.evictAll();
    CHECK_EQ(lib.residentBytes(), 0u);
    CHECK_EQ(lib.residentFiles(), 0u);
    const int32_t c = lib.fileIndex("colossus.gr2", 0.05f);
    CHECK(c >= 0);
    if (c < 0) return;
    CHECK(!lib.isResident(static_cast<uint32_t>(c)));
    const ModelLibrary::Buffers* b = nullptr;
    CHECK_OK(lib.resident(static_cast<uint32_t>(c), &b));
    CHECK(b != nullptr);
    CHECK(lib.isResident(static_cast<uint32_t>(c)));
    const ModelFile& f = lib.files()[static_cast<size_t>(c)];
    const uint64_t want = static_cast<uint64_t>(f.layout.vertexCount) * 20 + f.layout.skinnedVertexCount * 28ull +
                          static_cast<uint64_t>(f.layout.indexCount) * f.layout.indexBytes;
    CHECK_EQ(lib.residentBytes(), want);
    const ModelLibrary::Buffers* again = nullptr;
    CHECK_OK(lib.resident(static_cast<uint32_t>(c), &again));
    CHECK(again == b);                          // made once
    CHECK_EQ(lib.residentFiles(), 1u);
    // Refusals: out of range; a refused file.
    const ModelLibrary::Buffers* nb = reinterpret_cast<const ModelLibrary::Buffers*>(&lib);
    CHECK_ERR(lib.resident(static_cast<uint32_t>(lib.files().size()), &nb), Err::BadLayout);
    CHECK(nb == nullptr);
    const int32_t oil = lib.fileIndex("oil1.gr2", 1.0f);
    CHECK(oil >= 0);
    if (oil >= 0) CHECK_ERR(lib.resident(static_cast<uint32_t>(oil), &nb), Err::NotFound);
    // The whole set, an indication of its memory (landmark-renderer.md 3.3; not a timing claim).
    uint32_t refused = 0;
    for (uint32_t i = 0; i < lib.files().size(); ++i)
        if (lib.files()[i].ok && !lib.resident(i, nullptr).ok()) ++refused;
    CHECK_EQ(refused, 0u);
    std::printf("every file resident: %u files, %.1f MiB\n", lib.residentFiles(), lib.residentBytes() / 1048576.0);
    lib.evictAll();
    CHECK_EQ(lib.residentBytes(), 0u);
    CHECK(!lib.isResident(static_cast<uint32_t>(c)));
}

C5R_TEST(load_refusals) {
    lmt::Install& in = lmt::install(false);
    ModelLibrary lib;
    cities::ArtCatalogue empty;
    CHECK_ERR(lib.load(in.files, in.db, empty), Err::NotFound);        // the catalogue not loaded
    CHECK(!lib.loaded());
    CHECK_OK(in.artStatus);
    c5m::TsvTables none;                                               // a database without ArtDefine_Landmarks
    CHECK_ERR(lib.load(in.files, none, in.art), Err::NotFound);
    CHECK(!lib.loaded());
    CHECK(lib.files().empty());
    const ModelLibrary::Buffers* b = nullptr;
    CHECK_ERR(lib.resident(0, &b), Err::BadLayout);                    // nothing loaded
}

C5R_TEST_MAIN
