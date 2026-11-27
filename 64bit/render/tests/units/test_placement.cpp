// render/tests/units/test_placement.cpp - issue 4354 (epic 0041, U3). Synthetic data only (an invented formation file,
// a synthetic height field, a fake install folder in the test scratch): every check of c5r/units/placement.h and
// placement_check.h has a positive case and a negative case asserting the exact Err. The slot values were computed
// with the Python reference models/tools/formations/formations.py on the same invented file (2026-11-15).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

const char* kXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<Formations>\n"
    "  <Formation><Name>Bow</Name><SpacingX>10</SpacingX><SpacingY>9</SpacingY>"
    "<SpacingMultiplier>1.5</SpacingMultiplier><UnitRandomness>0.3</UnitRandomness><YOffsetInc>2</YOffsetInc>"
    "<YOffsetCurved>1</YOffsetCurved><TotalWidth>5</TotalWidth></Formation>\n"
    "  <Formation><Name>Vee</Name><SpacingX>7</SpacingX><SpacingY>8</SpacingY><UnitRandomness>0</UnitRandomness>"
    "<YOffsetInc>1.5</YOffsetInc><YOffsetCurved>0</YOffsetCurved><TotalWidth>4</TotalWidth><Stagger>1</Stagger>"
    "<PerRowInc>1</PerRowInc></Formation>\n"
    "  <Formation><Name>Blocks</Name><SpacingX>5</SpacingX><SpacingY>6</SpacingY><UnitRandomness>0.25</UnitRandomness>"
    "<YOffsetInc>1</YOffsetInc><YOffsetCurved>1</YOffsetCurved><TotalWidth>8</TotalWidth><XBlockSize>2</XBlockSize>"
    "<XBlockSpacingCount>1</XBlockSpacingCount><CenterWidth>2</CenterWidth><YBlockSize>1</YBlockSize>"
    "<YBlockSpacingCount>2</YBlockSpacingCount></Formation>\n"
    "  <Formation><Name>Gaps</Name><SpacingX>5</SpacingX><SpacingY>6</SpacingY><UnitRandomness>0</UnitRandomness>"
    "<YOffsetInc>1</YOffsetInc><YOffsetCurved>1</YOffsetCurved><TotalWidth>6</TotalWidth><XBlockSize>1</XBlockSize>"
    "<XBlockSpacingCount>2</XBlockSpacingCount></Formation>\n"
    "  <Formation><Name>Wide</Name><SpacingX>4</SpacingX><SpacingY>4</SpacingY><UnitRandomness>0.5</UnitRandomness>"
    "<TotalWidth>0</TotalWidth><YOffsetCurved>0</YOffsetCurved><YOffsetInc>0</YOffsetInc></Formation>\n"
    "  <Formation><Name>Line</Name><SpacingX>3</SpacingX><SpacingY>11</SpacingY><UnitRandomness>0</UnitRandomness>"
    "<TotalWidth>1</TotalWidth><YOffsetCurved>0</YOffsetCurved><YOffsetInc>0.5</YOffsetInc></Formation>\n"
    "  <Formation><Name>OneCurved</Name><TotalWidth>1</TotalWidth><YOffsetCurved>1</YOffsetCurved></Formation>\n"
    "</Formations>\n";

struct Gold {
    const char* name;
    uint32_t n, seed;
    std::vector<std::pair<float, float>> pts;
};
// formations.py slots() on the same records (2026-11-15).
const std::vector<Gold>& gold()
{
    static const std::vector<Gold> g = {
        {"Bow", 7, 25, {{-1.38496f, -7.43468f}, {-14.09219f, -5.68176f}, {15.65979f, -5.80706f}, {-30.25772f, -2.54280f},
                        {30.25772f, -1.74629f}, {-1.27313f, 5.18276f}, {17.07434f, 8.14095f}}},
        {"Vee", 9, 0, {{3.50000f, -5.50000f}, {-3.50000f, -5.50000f}, {10.50000f, -4.00000f}, {-10.50000f, -4.00000f},
                       {0.00000f, 2.50000f}, {-7.00000f, 4.00000f}, {7.00000f, 4.00000f}, {-14.00000f, 5.50000f},
                       {14.00000f, 5.50000f}}},
        {"Blocks", 11, 31, {{1.60193f, -9.55737f}, {-3.12597f, -9.29620f}, {12.11395f, -9.02632f},
                            {-12.80029f, -8.94168f}, {17.56962f, -7.79746f}, {-18.38084f, -7.32863f},
                            {27.58943f, -6.10281f}, {-27.58943f, -5.96701f}, {1.92181f, 7.74014f},
                            {-2.48849f, 8.02223f}, {11.81967f, 9.90010f}}},
        {"Gaps", 6, 3, {{7.50000f, -0.84549f}, {-7.50000f, -0.84549f}, {22.50000f, 0.65480f}, {-22.50000f, 0.65480f},
                        {37.50000f, 1.50000f}, {-37.50000f, 1.50000f}}},
        {"Wide", 5, 20, {{1.31424f, -2.13867f}, {-2.24704f, -2.16621f}, {6.56431f, -2.17001f}, {-6.56431f, -1.81649f},
                         {2.42914f, 2.17001f}}},
        {"Line", 3, 0, {{0.00000f, -11.00000f}, {0.00000f, 0.00000f}, {0.00000f, 11.00000f}}},
        {"", 1, 20, {{1.26076f, 0.10362f}}},
        {"", 4, 7, {{2.42637f, -0.57732f}, {-2.87164f, -1.31475f}, {9.13284f, 1.31475f}, {-9.13284f, 0.84496f}}},
    };
    return g;
}

FormationTable table()
{
    FormationTable t;
    Status st = t.parse(kXml);
    if (!st.ok()) ::c5r::test::fail(__FILE__, __LINE__, "table(): " + st.str());
    return t;
}

// A 4 x 2 plot map (mapH 6 with the 4 margin rows), heights rising with x, the nominal hex-to-world matrix.
heightfield::HeightGrid grid()
{
    heightfield::HeightGrid g;
    g.mapW = 4;
    g.mapH = 6;
    g.samplesX = g.mapW * 64;
    g.samplesY = g.mapH * 64;
    g.sx = 1.0f;
    g.sy = 0.866f;
    g.sz = 0.2f;
    g.ox = static_cast<float>(g.mapW * 32) * g.sx;
    g.oy = static_cast<float>(g.mapH * 32) * g.sy;
    g.waterZ = 5.0f;
    g.hexToWorld[0] = 64;
    g.hexToWorld[4] = 32;
    g.hexToWorld[5] = 64;
    g.hexToWorld[10] = 1;
    g.hexToWorld[12] = 32;
    g.hexToWorld[13] = 128;
    g.hexToWorld[15] = 1;
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(x * 40 + (y % 7));
    return g;
}

bool approx(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

bool writeFile(const std::wstring& path, const std::string& text)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

std::wstring scratch(const wchar_t* leaf)
{
    std::wstring w = gamefs::fromUtf8(::c5r::test::workDir());
    CreateDirectoryW(w.c_str(), nullptr);
    w += L"\\";
    w += leaf;
    CreateDirectoryW(w.c_str(), nullptr);
    return w;
}

}  // namespace

// ---- FormationTable ----

C5R_TEST(table_parse_ok)
{
    FormationTable t = table();
    CHECK_EQ(t.size(), static_cast<size_t>(7));
    const FormationRecord* bow = t.find("Bow");
    CHECK(bow != nullptr);
    if (!bow) return;
    CHECK_EQ(bow->spacingX, 10);
    CHECK(approx(bow->spacingMultiplier, 1.5f, 1e-6f));
    CHECK_EQ(bow->stagger, 0);         // not given: the Default record's value
    CHECK(approx(bow->blockRandomness, 0.2f, 1e-6f));
    const FormationRecord* def = t.find("");
    CHECK(def != nullptr && def->name == "Default" && def->spacingX == 6 && def->totalWidth == 4);
    CHECK(t.find("bow") == nullptr);   // exact names
    CHECK(t.find("Nope") == nullptr);
    CHECK(t.warnings().empty());
}

C5R_TEST(table_parse_unknown_field_is_a_warning)
{
    FormationTable t;
    CHECK_OK(t.parse("<Formations><Formation><Name>A</Name><Colour>3</Colour><SpacingX>2.9</SpacingX></Formation>"
                     "<Formation><Name>A</Name><SpacingX>5</SpacingX></Formation></Formations>"));
    CHECK_EQ(t.size(), static_cast<size_t>(1));
    CHECK_EQ(t.warnings().size(), static_cast<size_t>(2));   // unknown field, duplicate name
    CHECK_EQ(t.find("A")->spacingX, 5);                       // the later entry
    FormationTable u;
    CHECK_OK(u.parse("<Formations><Formation><Name>B</Name><SpacingX>2.9</SpacingX></Formation></Formations>"));
    CHECK_EQ(u.find("B")->spacingX, 2);                       // integer field truncated
}

C5R_TEST(table_parse_negative)
{
    FormationTable t;
    CHECK_ERR(t.parse("not xml <<<"), Err::BadLayout);
    CHECK_EQ(t.size(), static_cast<size_t>(0));
    CHECK(t.find("") == nullptr);
    CHECK_ERR(t.parse("<Other><Formation><Name>A</Name></Formation></Other>"), Err::BadLayout);
    CHECK_ERR(t.parse("<Formations><Formation><SpacingX>3</SpacingX></Formation></Formations>"), Err::BadLayout);
    CHECK_ERR(t.parse("<Formations><Formation><Name>A</Name><SpacingX>three</SpacingX></Formation></Formations>"),
              Err::BadLayout);
    CHECK_ERR(t.parse("<Formations><Formation><Name>A</Name><YOffsetInc>1.5x</YOffsetInc></Formation></Formations>"),
              Err::BadLayout);
    CHECK_ERR(t.parse("<Formations><Formation><Name>A</Name><SpacingX>9e99</SpacingX></Formation></Formations>"),
              Err::BadLayout);
    CHECK_ERR(t.parse("<Formations></Formations>"), Err::BadLayout);
    CHECK_EQ(t.size(), static_cast<size_t>(0));
}

C5R_TEST(table_load_from_install_folder)
{
    // a fake install: Assets/Units/UnitFormations.xml and UnitVisConfig.ini, empty Resource/
    std::wstring root = scratch(L"placement_fake_install");
    CreateDirectoryW((root + L"\\Resource").c_str(), nullptr);
    CreateDirectoryW((root + L"\\Assets").c_str(), nullptr);
    CreateDirectoryW((root + L"\\Assets\\Units").c_str(), nullptr);
    CHECK(writeFile(root + L"\\Assets\\Units\\UnitFormations.xml", kXml));
    CHECK(writeFile(root + L"\\Assets\\Units\\UnitVisConfig.ini", "[Other]\r\nGlobalUnitScale=7\r\n[Debugging]\r\n"
                                                                  "GlobalUnitScale = 150\r\n"));
    gamefs::GameFiles files;
    CHECK_OK(files.open(root));
    FormationTable t;
    CHECK_OK(t.load(files, root));
    CHECK_EQ(t.size(), static_cast<size_t>(7));
    CHECK(approx(t.unitScale(), 1.5f, 1e-6f));
    CHECK(t.origin().compare(0, 6, "loose:") == 0);
    FormationTable byName;   // no install root: the file system's name lookup
    CHECK_OK(byName.load(files, L""));
    CHECK_EQ(byName.size(), static_cast<size_t>(7));
    CHECK(byName.origin().compare(0, 6, "loose:") != 0);
    CHECK(approx(byName.unitScale(), 1.5f, 1e-6f));
    // without the .ini the scale is 1.0; without the .xml load fails with NotFound
    std::wstring root2 = scratch(L"placement_fake_install_noxml");
    CreateDirectoryW((root2 + L"\\Resource").c_str(), nullptr);
    CreateDirectoryW((root2 + L"\\Assets").c_str(), nullptr);
    gamefs::GameFiles empty;
    CHECK_OK(empty.open(root2));
    FormationTable u;
    CHECK_ERR(u.load(empty, root2), Err::NotFound);
    CHECK_ERR(u.load(empty, L""), Err::NotFound);
    CHECK_EQ(u.size(), static_cast<size_t>(0));
}

C5R_TEST(placement_columns)
{
    c5m::TsvTables db;
    c5m::Error e;
    CHECK(db.add("ArtDefine_UnitInfos", "Type\tFormation\nART_A\tBow\nART_B\t\\N\nART_C\t\n", &e));
    CHECK(db.add("ArtDefine_UnitMemberInfos", "Type\tZOffset\nM_A\t\\N\nM_B\t10.5\n", &e));
    PlacementColumns c;
    CHECK_OK(loadPlacementColumns(db, c));
    CHECK(c.formation["ART_A"] == "Bow");
    CHECK(c.formation["ART_B"].empty());
    CHECK(c.formation["ART_C"].empty());
    CHECK(approx(c.zOffset["M_A"], 0.0f, 0.0f));
    CHECK(approx(c.zOffset["M_B"], 10.5f, 0.0f));
    c5m::TsvTables bad;
    CHECK(bad.add("ArtDefine_UnitInfos", "Type\tFormation\nART_A\tBow\n", &e));
    CHECK_ERR(loadPlacementColumns(bad, c), Err::NotFound);          // no member table
    CHECK(c.formation.empty());
    CHECK(bad.add("ArtDefine_UnitMemberInfos", "Type\tZOffset\nM_A\tten\n", &e));
    CHECK_ERR(loadPlacementColumns(bad, c), Err::BadLayout);         // ZOffset not a number
    CHECK(c.zOffset.empty());
    c5m::TsvTables noCol;
    CHECK(noCol.add("ArtDefine_UnitInfos", "Type\nART_A\n", &e));
    CHECK_ERR(loadPlacementColumns(noCol, c), Err::NotFound);        // no Formation column
}

// ---- slots ----

C5R_TEST(slots_match_the_reference)
{
    FormationTable t = table();
    for (const Gold& gd : gold()) {
        const FormationRecord* r = t.find(gd.name);
        CHECK(r != nullptr);
        if (!r) continue;
        std::vector<Slot> s;
        CHECK_OK(formationSlots(*r, gd.n, gd.seed, true, 1.0f, s));
        CHECK_EQ(s.size(), gd.pts.size());
        for (size_t i = 0; i < s.size() && i < gd.pts.size(); ++i) {
            if (!approx(s[i].x, gd.pts[i].first, 2e-4f) || !approx(s[i].y, gd.pts[i].second, 2e-4f))
                ::c5r::test::fail(__FILE__, __LINE__, std::string("slot ") + std::to_string(i) + " of '" + gd.name +
                                                          "': " + std::to_string(s[i].x) + ", " + std::to_string(s[i].y));
        }
    }
}

C5R_TEST(slots_grid_and_scale)
{
    FormationTable t = table();
    std::vector<Slot> a, b;
    CHECK_OK(formationSlots(*t.find("Vee"), 4, 0, false, 1.0f, a));
    CHECK_OK(formationSlots(*t.find("Vee"), 4, 0, false, 2.0f, b));
    for (size_t i = 0; i < 4; ++i) CHECK(approx(b[i].x, 2.0f * a[i].x, 1e-5f));   // spacings scale, the bend does not
    // the random part: two seeds differ, the same seed repeats
    std::vector<Slot> c, d, e;
    CHECK_OK(formationSlots(*t.find("Bow"), 7, 25, true, 1.0f, c));
    CHECK_OK(formationSlots(*t.find("Bow"), 7, 26, true, 1.0f, d));
    CHECK_OK(formationSlots(*t.find("Bow"), 7, 25, true, 1.0f, e));
    CHECK(!approx(c[0].x, d[0].x, 1e-3f));
    CHECK(approx(c[3].y, e[3].y, 0.0f));
}

C5R_TEST(slots_negative)
{
    FormationTable t = table();
    std::vector<Slot> s;
    CHECK_ERR(formationSlots(*t.find("Bow"), 0, 0, true, 1.0f, s), Err::BadLayout);
    CHECK_ERR(formationSlots(*t.find("Bow"), kMaxFigures + 1, 0, true, 1.0f, s), Err::BadLayout);
    CHECK_ERR(formationSlots(*t.find("OneCurved"), 2, 0, true, 1.0f, s), Err::BadLayout);   // 0 / 0 in the bend
    CHECK(s.empty());
    CHECK_ERR(formationSlots(*t.find("Bow"), 3, 0, true, std::nanf(""), s), Err::BadLayout);
}

C5R_TEST(rng_steps)
{
    FormationRng r;
    r.state = 0;
    const float u = r.next();
    CHECK_EQ(r.state, 2531011u);
    CHECK(approx(u, static_cast<float>(2531011.0 / 4294954991.0), 1e-9f));
    r.state = 0xFFFFFFFFu;   // wraps mod 2^32, then reduced modulo 0xFFFFCFEF
    r.next();
    CHECK(r.state < 0xFFFFCFEFu);
}

C5R_TEST(facing_yaw)
{
    const float d2r = 0.017453292519943295f;
    CHECK(approx(facingYaw(0), 150 * d2r, 1e-6f));
    CHECK(approx(facingYaw(1), 90 * d2r, 1e-6f));
    CHECK(approx(facingYaw(2), 30 * d2r, 1e-6f));
    CHECK(approx(facingYaw(3), 330 * d2r, 1e-5f));
    CHECK(approx(facingYaw(4), 270 * d2r, 1e-5f));
    CHECK(approx(facingYaw(5), 210 * d2r, 1e-5f));
    CHECK(approx(facingYaw(-1), 1.0f, 0.0f));   // NO_DIRECTION and anything else: 1 rad, as the EXE
    CHECK(approx(facingYaw(6), 1.0f, 0.0f));
}

C5R_TEST(living_figures)
{
    uint32_t l = 99;
    CHECK_OK(livingFigures(12, 0, 100, -1, l));
    CHECK_EQ(l, 12u);
    CHECK_OK(livingFigures(12, 50, 100, -1, l));
    CHECK_EQ(l, 6u);
    CHECK_OK(livingFigures(12, 99, 100, -1, l));   // round(11.88) = 12, capped at n - 1 below 100 percent
    CHECK_EQ(l, 1u);
    CHECK_OK(livingFigures(12, 100, 100, -1, l));
    CHECK_EQ(l, 0u);
    CHECK_OK(livingFigures(3, 17, 100, -1, l));    // round(0.51) = 1 dead
    CHECK_EQ(l, 2u);
    CHECK_OK(livingFigures(3, 16, 100, -1, l));    // round(0.48) = 0 dead
    CHECK_EQ(l, 3u);
    CHECK_OK(livingFigures(12, 120, 100, -1, l));  // damage above max: all dead
    CHECK_EQ(l, 0u);
    CHECK_OK(livingFigures(12, 0, 100, 5, l));     // override
    CHECK_EQ(l, 5u);
    CHECK_ERR(livingFigures(12, 0, 100, 13, l), Err::BadLayout);
    CHECK_ERR(livingFigures(12, 0, 100, -2, l), Err::BadLayout);
    CHECK_ERR(livingFigures(12, 0, 0, -1, l), Err::BadLayout);
    CHECK_ERR(livingFigures(12, -1, 100, -1, l), Err::BadLayout);
    CHECK_ERR(livingFigures(0, 0, 100, -1, l), Err::BadLayout);
    CHECK_ERR(livingFigures(kMaxFigures + 1, 0, 100, -1, l), Err::BadLayout);
}

// ---- plot centre and heights ----

C5R_TEST(plot_centre_nominal_and_tapped)
{
    heightfield::HeightGrid g = grid();
    float x = 0, y = 0;
    CHECK_OK(plotCentre(g, 1, 1, x, y));   // hx = 1, hy = 1: sample (128, 192)
    CHECK(approx(x, 128.0f - g.ox, 1e-4f));
    CHECK(approx(y, 192.0f * g.sy - g.oy, 1e-4f));
    CHECK_OK(plotCentre(g, 2, 0, x, y));   // even row: sample (160, 128)
    CHECK(approx(x, 160.0f - g.ox, 1e-4f));
    // the tapped matrix's own values move the centre (the 0.05-0.12-unit shift of unit-formations.md 12.3)
    g.hexToWorld[0] = 63.9986f;
    g.hexToWorld[1] = 0.000938415644f;
    g.hexToWorld[5] = 64.0018845f;
    g.hexToWorld[12] = 32.0007057f;
    g.hexToWorld[13] = 128.003296f;
    CHECK_OK(plotCentre(g, 2, 1, x, y));   // hx = 2, hy = 1
    CHECK(approx(x, (2 * 63.9986f + 32 + 32.0007057f) - g.ox, 1e-4f));
    CHECK(approx(y, (2 * 0.000938415644f + 64.0018845f + 128.003296f) * g.sy - g.oy, 1e-4f));
}

C5R_TEST(plot_centre_negative)
{
    heightfield::HeightGrid g = grid();
    float x = 0, y = 0;
    CHECK_ERR(plotCentre(g, 4, 0, x, y), Err::BadLayout);    // x = mapW
    CHECK_ERR(plotCentre(g, 0, 2, x, y), Err::BadLayout);    // y = mapH - 4
    CHECK_ERR(plotCentre(g, -1, 0, x, y), Err::BadLayout);
    CHECK_ERR(plotCentre(g, 0, -1, x, y), Err::BadLayout);
    heightfield::HeightGrid z = grid();
    for (float& v : z.hexToWorld) v = 0;
    CHECK_ERR(plotCentre(z, 0, 0, x, y), Err::BadLayout);    // no matrix
    heightfield::HeightGrid n = grid();
    n.hexToWorld[3] = std::nanf("");
    CHECK_ERR(plotCentre(n, 0, 0, x, y), Err::BadLayout);
    heightfield::HeightGrid e;
    CHECK_ERR(plotCentre(e, 0, 0, x, y), Err::BadLayout);    // empty field
    heightfield::HeightGrid s = grid();
    s.sx = 0;
    CHECK_ERR(plotCentre(s, 0, 0, x, y), Err::BadLayout);
}

C5R_TEST(height_truncates)
{
    heightfield::HeightGrid g = grid();
    // world (x, y) -> sample column (x + ox) / sx, row (y + oy) / sy, each truncated
    const float y0 = 10.0f * g.sy - g.oy;
    CHECK(approx(heightAtWorld(g, 20.9f - g.ox, y0 + 0.1f), heightfield::heightAtSample(g, 20, 10), 0.0f));
    CHECK(approx(heightAtWorld(g, 21.0f - g.ox, y0 + 0.1f), heightfield::heightAtSample(g, 21, 10), 0.0f));
    // -0.5 truncates to column 0 (floor would give -1, outside: 0)
    CHECK(approx(heightAtWorld(g, -0.5f - g.ox, y0 + 0.1f), heightfield::heightAtSample(g, 0, 10), 0.0f));
    CHECK(heightfield::heightAtSample(g, 0, 10) > 0.0f);
    CHECK(approx(heightAtWorld(g, -5.0f - g.ox, y0), 0.0f, 0.0f));   // outside the grid
    CHECK(approx(heightAtWorld(g, std::nanf(""), y0), 0.0f, 0.0f));
}

// ---- one unit ----

C5R_TEST(place_unit)
{
    FormationTable t = table();
    heightfield::HeightGrid g = grid();
    PlacementInput in;
    in.formation = "Bow";
    in.zOffset.assign(7, 0.0f);
    in.zOffset[2] = 1.5f;
    in.plotX = 1;
    in.plotY = 1;
    in.facing = 2;
    in.seed = 25;
    in.waterLevel = 0.0f;
    UnitPlacement p;
    CHECK_OK(placeUnit(t, g, in, p));
    float cx = 0, cy = 0;
    CHECK_OK(plotCentre(g, 1, 1, cx, cy));
    CHECK(approx(p.rootX, cx, 0.0f) && approx(p.rootY, cy, 0.0f));
    CHECK_EQ(p.living, 7u);
    CHECK_EQ(p.placed.size(), static_cast<size_t>(7));
    const float c = std::cos(facingYaw(2)), s = std::sin(facingYaw(2));
    for (size_t i = 0; i < p.placed.size(); ++i) {
        const std::pair<float, float>& q = gold()[0].pts[i];
        CHECK(approx(p.placed[i].x, cx + c * q.first - s * q.second, 3e-4f));
        CHECK(approx(p.placed[i].y, cy + s * q.first + c * q.second, 3e-4f));
        CHECK(approx(p.placed[i].z, std::max(heightAtWorld(g, p.placed[i].x, p.placed[i].y), 0.0f) + in.zOffset[i], 1e-5f));
        CHECK(approx(p.placed[i].yaw, facingYaw(2), 0.0f));
        CHECK_EQ(p.placed[i].figure, static_cast<uint16_t>(i));
    }
    // the water level is the floor
    in.waterLevel = 1000.0f;
    CHECK_OK(placeUnit(t, g, in, p));
    CHECK(approx(p.placed[0].z, 1000.0f, 0.0f));
    CHECK(approx(p.placed[2].z, 1001.5f, 0.0f));
    // the dead are the last members; the living keep the slots of the 7-member layout
    in.waterLevel = 0.0f;
    in.alive = 3;
    UnitPlacement q;
    CHECK_OK(placeUnit(t, g, in, q));
    CHECK_EQ(q.placed.size(), static_cast<size_t>(3));
    CHECK_EQ(q.figures, 7u);
    for (size_t i = 0; i < 3; ++i) CHECK(approx(q.placed[i].x, p.placed[i].x, 0.0f) && approx(q.placed[i].y, p.placed[i].y, 0.0f));
    in.alive = -1;
    in.damage = 50;
    CHECK_OK(placeUnit(t, g, in, q));
    CHECK_EQ(q.living, 3u);   // round(3.5) = 4 dead of 7
    in.damage = 0;
    // position source exe: the root replaces the plot centre
    in.hasRoot = true;
    in.rootX = cx + 10.0f;
    in.rootY = cy - 4.0f;
    CHECK_OK(placeUnit(t, g, in, q));
    CHECK(approx(q.placed[1].x, p.placed[1].x + 10.0f, 1e-3f));
    CHECK(approx(q.placed[1].y, p.placed[1].y - 4.0f, 1e-3f));
    // an empty formation name is Default
    PlacementInput d;
    d.zOffset.assign(1, 0.0f);
    d.seed = 20;
    CHECK_OK(placeUnit(t, g, d, q));
    CHECK_EQ(q.placed.size(), static_cast<size_t>(1));
}

C5R_TEST(place_unit_negative)
{
    FormationTable t = table();
    heightfield::HeightGrid g = grid();
    PlacementInput ok;
    ok.formation = "Vee";
    ok.zOffset.assign(4, 0.0f);
    UnitPlacement p;
    CHECK_OK(placeUnit(t, g, ok, p));
    PlacementInput in = ok;
    in.formation = "NoSuchFormation";
    CHECK_ERR(placeUnit(t, g, in, p), Err::NotFound);
    CHECK(p.placed.empty());
    FormationTable empty;
    CHECK_ERR(placeUnit(empty, g, ok, p), Err::NotFound);
    in = ok;
    in.plotX = 4;
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in.hasRoot = true;   // the plot is checked in both position sources
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.zOffset.clear();
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in.zOffset.assign(kMaxFigures + 1, 0.0f);
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.zOffset[1] = std::nanf("");
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.alive = 5;
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.hasRoot = true;
    in.rootX = std::nanf("");
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.waterLevel = std::nanf("");
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.seed = 0xFFFFFFFFu;
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    in = ok;
    in.formation = "OneCurved";
    in.zOffset.assign(2, 0.0f);
    CHECK_ERR(placeUnit(t, g, in, p), Err::BadLayout);
    heightfield::HeightGrid z = grid();
    for (float& v : z.hexToWorld) v = 0;
    CHECK_ERR(placeUnit(t, z, ok, p), Err::BadLayout);
    CHECK(p.placed.empty());
}

// ---- the check ----

C5R_TEST(assignment)
{
    std::vector<size_t> col;
    minCostAssignment({4, 1, 3, 2, 0, 5, 3, 2, 2}, 3, col);
    CHECK_EQ(col.size(), static_cast<size_t>(3));
    CHECK(col[0] == 1 && col[1] == 0 && col[2] == 2);   // 1 + 2 + 2 = 5
    minCostAssignment({7}, 1, col);
    CHECK_EQ(col[0], static_cast<size_t>(0));
}

C5R_TEST(catalogue_parse_and_check)
{
    FormationTable t = table();
    heightfield::HeightGrid g = grid();
    PlacementColumns cols;
    cols.formation["ART_BOW"] = "Bow";
    cols.formation["ART_ONE"] = "";
    cols.zOffset["M_A"] = 0.0f;
    cols.zOffset["M_B"] = 0.0f;
    // measured figures = the rule's own output (drawn in reverse order), one unit moved by 0.01
    PlacementInput in;
    in.formation = "Bow";
    in.zOffset.assign(7, 0.0f);
    in.plotX = 1;
    in.plotY = 1;
    in.seed = 25;
    in.waterLevel = g.waterZ;
    UnitPlacement p;
    CHECK_OK(placeUnit(t, g, in, p));
    std::string text = "# test\nc5placement-catalogue\t1\nunit\tL01\t1\tART_BOW\t1\t1\t2\t25\n"
                       "member\tM_A\t3\t-\nmember\tM_B\t4\t-\n";
    char buf[256];
    for (size_t k = 0; k < 7; ++k) {
        const size_t i = 6 - k;
        std::snprintf(buf, sizeof buf, "fig\t%s\t%.4f\t%.4f\t%.4f\t30.0000\t0.00010\t0\n", i < 3 ? "M_A" : "M_B",
                      p.placed[i].x, p.placed[i].y, p.placed[i].z);
        text += buf;
    }
    text += "unit\tL01\t2\tART_BOW\t1\t1\t2\t25\nmember\tM_A\t7\tAir\n";
    for (size_t i = 0; i < 7; ++i) {
        std::snprintf(buf, sizeof buf, "fig\tM_A\t%.4f\t%.4f\t%.4f\t30.0000\t0.00010\t0\n", p.placed[i].x + 0.01f,
                      p.placed[i].y, p.placed[i].z + 0.5f);
        text += buf;
    }
    text += "unit\tL01\t3\tART_ONE\t0\t0\t2\t20\nmember\tM_A\t2\t-\nfig\tM_A\t1\t2\t3\t30\t0\t0\r\n";   // 1 of 2
    text += "unit\tL01\t4\tART_GONE\t0\t0\t2\t20\nmember\tM_A\t1\t-\nfig\tM_A\t1\t2\t3\t30\t0\t0\n";   // no art row
    text += "unit\tL01\t5\tART_ONE\t0\t0\t2\t20\nmember\tM_A\t1\t-\n";                               // no figures
    std::vector<CatalogueUnit> units;
    CHECK_OK(parseCatalogue(text, units));
    CHECK_EQ(units.size(), static_cast<size_t>(5));
    CHECK_EQ(units[0].members.size(), static_cast<size_t>(2));
    CHECK_EQ(units[0].figures.size(), static_cast<size_t>(7));
    CatalogueReport r;
    CheckTolerances tol;
    CHECK_OK(checkCatalogue(t, g, cols, units, g.waterZ, tol, r));
    CHECK_EQ(r.multi, 2);
    CHECK_EQ(r.multiWithin, 1);
    CHECK_EQ(r.failed, 1);
    CHECK_EQ(r.notCompared, 2);
    CHECK(r.units[0].within && r.units[0].maxDxy < 1e-3f);
    CHECK(!r.units[1].within && approx(r.units[1].maxDxy, 0.01f, 1e-3f));
    CHECK_EQ(r.units[1].figures[0].heightClass, std::string("air"));
    CHECK_EQ(r.units[1].outsideClass, std::string("air"));
    CHECK_EQ(r.multiOutsideAir, 1);
    CHECK_EQ(r.units[2].kind, std::string("figures!=members"));
    CHECK_EQ(r.units[3].kind, std::string("failed"));
    CHECK_EQ(r.units[4].kind, std::string("no-figures"));
    // members pair only with figures of their own type: unit 0's first placed figure (M_A) is a measured M_A
    CHECK_EQ(r.units[0].figures[0].measured, 6);
    const std::string rep = catalogueReportText(r, tol);
    CHECK(rep.find("multi-figure units within: 1 of 2") != std::string::npos);
    CHECK(rep.find("# outside: L01 2 ART_BOW (multi, Bow, class air)") != std::string::npos);
    // a figure whose meshes disagree (part spread) is a cross-fade: class fit
    units[1].members[0].domain.clear();
    units[1].figures[3].partSpread = 0.01f;
    CHECK_OK(checkCatalogue(t, g, cols, units, g.waterZ, tol, r));
    CHECK_EQ(r.units[1].outsideClass, std::string("fit"));
    units[1].figures[3].partSpread = 0.0f;
    CHECK_OK(checkCatalogue(t, g, cols, units, g.waterZ, tol, r));
    CHECK_EQ(r.units[1].outsideClass, std::string("other"));
    CHECK_EQ(r.multiOutsideOther, 1);
}

C5R_TEST(catalogue_parse_negative)
{
    std::vector<CatalogueUnit> u;
    CHECK_ERR(parseCatalogue("unit\tL01\t1\tA\t0\t0\t2\t20\n", u), Err::BadVersion);
    CHECK_ERR(parseCatalogue("c5placement-catalogue\t2\n", u), Err::BadVersion);
    CHECK_ERR(parseCatalogue("", u), Err::BadVersion);
    const std::string h = "c5placement-catalogue\t1\n";
    CHECK_ERR(parseCatalogue(h + "member\tM\t1\t-\n", u), Err::BadLayout);                       // orphan
    CHECK_ERR(parseCatalogue(h + "fig\tM\t1\t2\t3\t30\t0\t0\n", u), Err::BadLayout);             // orphan
    CHECK_ERR(parseCatalogue(h + "unit\tL01\t1\tA\t0\t0\t2\n", u), Err::BadLayout);              // 7 fields
    CHECK_ERR(parseCatalogue(h + "unit\tL01\tx\tA\t0\t0\t2\t20\n", u), Err::BadLayout);          // number
    CHECK_ERR(parseCatalogue(h + "unit\tL01\t1\tA\t0\t0\t2\t-3\n", u), Err::BadLayout);          // seed range
    CHECK_ERR(parseCatalogue(h + "unit\tL01\t1\tA\t0\t0\t2\t20\nmember\tM\t0\t-\n", u), Err::BadLayout);
    CHECK_ERR(parseCatalogue(h + "unit\tL01\t1\tA\t0\t0\t2\t20\nfig\tM\t1\t2\t3e\t30\t0\t0\n", u), Err::BadLayout);
    CHECK_ERR(parseCatalogue(h + "unit\tL01\t1\tA\t0\t0\t2\t20\nfig\tM\t1\t2\t3\t30\t0\n", u), Err::BadLayout);
    CHECK_ERR(parseCatalogue(h + "units\tL01\n", u), Err::BadLayout);                              // unknown record
    CHECK(u.empty());
}

C5R_TEST_MAIN
