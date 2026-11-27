// render/tests/units/test_placement_catalogue.cpp - issue 4354 (epic 0041, U3), DoD D: every unit of 3907's catalogue
// placed by the product's rule (placeUnit) and compared with 0327's exact figure positions.
//
// Inputs (read only): the install's UnitFormations.xml (C5R_GAME_DIR); the height field of bundle
// C5H-0301-TINY_0009_20261115-225124_workstation (format version 3, task 0364, which retired the version-2 bundle
// ..._20261115-070309_workstation; its save is the catalogue's, SHA-256 b2ceb663...dcc29163;
// C5R_UNITS_HEIGHTFIELD overrides the path; issue 4470: without it the bundle is looked for in the library's bundles/
// and then in retired/, where epic 0375's swap to format version 4 moves it, offline-scene-harness.md 21; the format-4
// Tiny bundle's height field is not byte-equal, so this test keeps the bundle its numbers were made on); the
// catalogue's database copy (C5R_UNITS_DB); the catalogue text
// models/evidence/0041/4354/catalogue_figures.tsv (C5R_UNITS_CATALOGUE). A missing input skips, and build.py counts a
// skip as a failure. The report goes to C5R_UNITS_PLACEMENT_REPORT, else <C5R_WORK>/placement_catalogue_report.tsv.
// Results: research/05-graphics-composition/unit-renderer.md section 12.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "c5models/art.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/units/placement.h"
#include "c5r/units/placement_check.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::units;

namespace {

std::string env(const char* name, const std::string& def)
{
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : def;
}

bool readAll(const std::string& path, std::string& out)
{
    std::ifstream f(gamefs::fromUtf8(path).c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string sourceDir()
{
    std::string f = __FILE__;
    size_t s = f.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : f.substr(0, s);
}

}  // namespace

C5R_TEST(catalogue_placement)
{
    const std::string game = ::c5r::test::gameDir();
    const std::string hfTail = "/C5H-0301-TINY_0009_20261115-225124_workstation/heightfield/load0003.c5hf";
    const std::string hfLibrary = "F:/c5-64bit-work/scene-library/bundles" + hfTail;
    const std::string hfRetired = "F:/c5-64bit-work/scene-library/retired" + hfTail;   // after 0375's swap (4470)
    std::string hfPath = env("C5R_UNITS_HEIGHTFIELD", "");
    const std::string dbPath = env("C5R_UNITS_DB", "F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db");
    const std::string catPath =
        env("C5R_UNITS_CATALOGUE", sourceDir() + "/../../../models/evidence/0041/4354/catalogue_figures.tsv");
    std::string hfBytes, catText;
    if (hfPath.empty())
        hfPath = readAll(hfLibrary, hfBytes) ? hfLibrary : hfRetired;
    if (hfBytes.empty() && !readAll(hfPath, hfBytes))
        ::c5r::test::skip("height field not found: " + hfPath + (hfPath == hfRetired ? " (nor " + hfLibrary + ")" : ""));
    if (!readAll(catPath, catText)) ::c5r::test::skip("catalogue text not found: " + catPath);
    if (!c5m::SqliteTables::available()) ::c5r::test::skip("winsqlite3.dll not available");
    c5m::SqliteTables db;
    c5m::Error e;
    if (!db.open(dbPath, &e)) ::c5r::test::skip("database not opened: " + dbPath + ": " + e.what);

    gamefs::GameFiles files;
    CHECK_OK(files.open(gamefs::fromUtf8(game)));
    FormationTable table;
    CHECK_OK(table.load(files, gamefs::fromUtf8(game)));
    CHECK_EQ(table.size(), static_cast<size_t>(24));   // unit-formations.md 0: 24 <Formation> entries
    CHECK(table.unitScale() == 1.0f);
    std::printf("formations: %zu from %s, unit scale %.3f\n", table.size(), table.origin().c_str(), table.unitScale());
    FormationTable byName;   // the file system's name lookup alone returns the pack's older copy
    CHECK_OK(byName.load(files, L""));
    std::printf("name lookup alone: %zu formations from %s\n", byName.size(), byName.origin().c_str());

    heightfield::HeightGrid g;
    CHECK_OK(heightfield::readTap(reinterpret_cast<const uint8_t*>(hfBytes.data()), hfBytes.size(), g));
    PlacementColumns cols;
    CHECK_OK(loadPlacementColumns(db, cols));
    std::vector<CatalogueUnit> units;
    CHECK_OK(parseCatalogue(catText, units));
    CHECK_EQ(units.size(), static_cast<size_t>(229));

    CheckTolerances tol;
    CatalogueReport r;
    CHECK_OK(checkCatalogue(table, g, cols, units, g.waterZ, tol, r));
    const std::string rep = catalogueReportText(r, tol);
    const std::string outPath = env("C5R_UNITS_PLACEMENT_REPORT", ::c5r::test::workDir() + "/placement_catalogue_report.tsv");
    {
        std::ofstream o(gamefs::fromUtf8(outPath).c_str(), std::ios::binary);
        o << rep;
        CHECK(static_cast<bool>(o));
    }
    // the summary block
    size_t at = rep.find("# summary");
    std::printf("%s", at == std::string::npos ? rep.c_str() : rep.c_str() + at);
    std::printf("report: %s\n", outPath.c_str());

    // DoD D. 0327 had 134 of 138 multi-figure units within 0.002 after fitting a common shift per unit; here
    // nothing is fitted: the plot centre is the EXE's HexToWorld with the tapped matrix (P6). Results of
    // 2026-11-15 (unit-renderer.md 12): 129 of 138 within, the 9 others air (4) or cross-fade measurements (4) and
    // one fit-noise figure (the paratrooper, 0.0023); these numbers are floors for regressions.
    CHECK_EQ(r.multi, 138);
    CHECK_EQ(r.failed, 0);
    CHECK(r.multiWithin >= 129);
    CHECK(r.multiOutsideOther <= 1);
    CHECK_EQ(r.heightFigures, 1135);
    CHECK(r.heightWithin >= 1083);
    int otherHeights = 0;
    for (const UnitCheck& u : r.units)
        for (const FigureCheck& f : u.figures) otherHeights += f.heightClass == "other" ? 1 : 0;
    std::printf("height misses of class other: %d\n", otherHeights);
}

C5R_TEST_MAIN
