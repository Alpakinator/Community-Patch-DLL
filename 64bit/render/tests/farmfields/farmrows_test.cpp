// render/tests/farmfields/farmrows_test.cpp - the model library's FARM rows (ticket 0653, D230): the rows the farm
// buildings choose from, read from the player's install at run time (D83), and their models handed out by row.
//
// Reads the install (C5R_GAME_DIR) and the database copy through the landmark tests' fixture
// (render/tests/landmarks/landmarks_testkit.h: C5R_LANDMARK_DB, else C5R_UNITS_DB, else the units tests' default copy);
// a missing input skips, and a skip counts as a failure (render/build.py --test). The negative tests (an unloaded
// library, rows out of range) need no install and run first.
#include <string>
#include <vector>

#include "../landmarks/landmarks_testkit.h"
#include "c5r/farmfields/buildings.h"
#include "c5r/landmarks/library.h"
#include "testkit.h"

using namespace c5r;

C5R_TEST(a_library_not_loaded_refuses)
{
    landmarks::ModelLibrary lib;
    farmfields::FarmRowChoice r;
    r.level[1] = {5};
    CHECK_ERR(lib.farmRows(r), Err::NotFound);
    CHECK(!r.any());   // the output is cleared
    CHECK_EQ(lib.rowFile(0), -1);
    CHECK_EQ(lib.rowFile(-1), -1);
    CHECK_EQ(lib.farmBuildingModel(0, 2), -1);
    CHECK_EQ(lib.farmBuildingModel(-1, 3), -1);
}

C5R_TEST(farm_rows_of_the_install)
{
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    const landmarks::ModelLibrary& lib = in.lib;
    farmfields::FarmRowChoice r;
    CHECK_OK(lib.farmRows(r));
    std::printf("FARM rows %u; levels 0-4: %zu %zu %zu %zu %zu; refused %zu\n", r.farmHandlerRows, r.level[0].size(),
                r.level[1].size(), r.level[2].size(), r.level[3].size(), r.level[4].size(), r.refused.size());
    for (const farmfields::FarmRowRefusal& f : r.refused) std::printf("  refused: %s\n", f.text.c_str());
    // CP + VP (cities-and-landmarks.md 2.3; 0653 step 0's query on the huge save's database): 24 FARM rows, six
    // farm-building rows: Medieval_Farm1-3 (Ancient -> level 1) and Modern_Farm1-3 (Industrial -> level 3)
    CHECK(r.farmHandlerRows >= 6);
    CHECK_EQ(r.level[1].size(), static_cast<size_t>(3));
    CHECK_EQ(r.level[3].size(), static_cast<size_t>(3));
    CHECK(r.level[0].empty() && r.level[2].empty() && r.level[4].empty());
    CHECK(r.refused.empty());
    for (uint32_t l = 0; l < farmfields::kFarmEraLevels; ++l)
        for (int32_t row : r.level[l]) {
            CHECK(row >= 0 && static_cast<size_t>(row) < lib.landmarkRows().size());
            const landmarks::LandmarkRow& lr = lib.landmarkRows()[row];
            CHECK(lr.handler == landmarks::Handler::Farm);
            CHECK_EQ(lr.row.improvement, std::string("ART_DEF_IMPROVEMENT_FARM"));
            CHECK(lr.row.model.find(l == 1 ? "Medieval_Farm" : "Modern_Farm") != std::string::npos);
            // the model by row: a laid-out file with parts, its buffers made resident on demand
            const int32_t f = lib.rowFile(row);
            CHECK(f >= 0);
            if (f < 0) continue;
            CHECK(lib.files()[f].ok);
            CHECK(!lib.files()[f].layout.parts.empty());
            std::printf("  level %u row %d %s -> %s (%zu parts)\n", l, row, lr.row.model.c_str(), lib.files()[f].gr2.c_str(),
                        lib.files()[f].layout.parts.size());
            // one model per state: built, half built (HB_), pillaged (PL_), each with at least one packed part
            const int32_t mb = lib.farmBuildingModel(f, 2), mh = lib.farmBuildingModel(f, 1), mp = lib.farmBuildingModel(f, 3);
            CHECK(mb >= 0 && mh >= 0 && mp >= 0 && mb != mh && mb != mp && mh != mp);
            if (mb < 0 || mh < 0 || mp < 0) continue;
            CHECK(!lib.files()[f].modelParts[mb].empty() && !lib.files()[f].modelParts[mh].empty() &&
                  !lib.files()[f].modelParts[mp].empty());
            std::printf("    built %s (%zu parts), half built %s, pillaged %s\n", lib.files()[f].models[mb].c_str(),
                        lib.files()[f].modelParts[mb].size(), lib.files()[f].models[mh].c_str(),
                        lib.files()[f].models[mp].c_str());
        }
    // the 18 resource rows are counted, not chosen
    CHECK_EQ(lib.rowFile(static_cast<int32_t>(lib.landmarkRows().size())), -1);
}

C5R_TEST(farm_row_models_become_resident)
{
    lmt::Install& in = lmt::install(true);
    CHECK_OK(in.libStatus);
    farmfields::FarmRowChoice r;
    CHECK_OK(in.lib.farmRows(r));
    CHECK(!r.level[1].empty());
    if (r.level[1].empty()) return;
    const int32_t f = in.lib.rowFile(r.level[1][0]);
    CHECK(f >= 0);
    if (f < 0) return;
    const landmarks::ModelLibrary::Buffers* b = nullptr;
    CHECK_OK(in.lib.resident(static_cast<uint32_t>(f), &b));
    CHECK(b && !b->indices.empty() && !b->vertices.empty());
    in.lib.release(static_cast<uint32_t>(f));
}

C5R_TEST_MAIN
