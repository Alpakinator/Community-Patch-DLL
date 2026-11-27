// test_catalogue.cpp - c5r::cities' art catalogue (ticket 4872): the city-set parser, the three conversions and the
// wonder forms on synthetic input (no game files; a negative test per check), then the catalogue over the install and
// the database copy (landmarks_testkit.h) against cities-and-landmarks.md 1.2-1.10.
#include <cstring>
#include <string>
#include <vector>

#include "landmarks_testkit.h"

using namespace c5r;
using namespace c5r::cities;

namespace {

Status parse(const std::string& xml, std::vector<CitySet>& out) {
    return parseCitySetFile(xml.data(), xml.size(), "test.xml", out);
}

const char* kTwo =
    "<?xml version=\"1.0\"?>\n<BuildingArtInfos>\n"
    "  <!-- a comment -->\n"
    "  <Culture name=\"europe\">\n"
    "    <Era name=\"Ancient\"><PopulationBuildings><Granny>Euro_Anc_City.gr2</Granny></PopulationBuildings></Era>\n"
    "    <Era name=\"Rennaisance\"><PopulationBuildings><Granny>Art/Euro_Ren_City.gr2</Granny></PopulationBuildings></Era>\n"
    "  </Culture>\n"
    "  <Culture name=\"Mediterranean\">\n"
    "    <Era name=\"x\"><PopulationBuildings><Granny>Medit_Anc_City.gr2</Granny></PopulationBuildings></Era>\n"
    "  </Culture>\n"
    "  <Culture name=\"Polynesian\" lighting=\"Asia\">\n"
    "    <Era name=\"y\"><PopulationBuildings><Granny>Poly_Anc_City.gr2</Granny></PopulationBuildings></Era>\n"
    "  </Culture>\n"
    "</BuildingArtInfos>\n";

std::string culture(const std::string& attrs, int eras) {
    std::string s = "<BuildingArtInfos><Culture " + attrs + ">";
    for (int i = 0; i < eras; ++i) s += "<Era name=\"e\"><PopulationBuildings><Granny>a.gr2</Granny></PopulationBuildings></Era>";
    return s + "</Culture></BuildingArtInfos>";
}

}  // namespace

C5R_TEST(city_set_file_parsed) {
    std::vector<CitySet> v;
    CHECK_OK(parse(kTwo, v));
    CHECK_EQ(v.size(), 4u);
    if (v.size() != 4) return;
    CHECK_EQ(v[0].culture, 3u);                      // names match without case
    CHECK_EQ(v[0].eraSlot, 0u);
    CHECK_EQ(v[1].eraSlot, 1u);                      // positional: the era's name is never compared
    CHECK_EQ(v[1].eraName, std::string("Rennaisance"));
    CHECK_EQ(v[1].granny, std::string("euro_ren_city.gr2"));   // lower-case base name
    CHECK_EQ(v[0].lighting, 3u);
    CHECK_EQ(v[2].culture, 4u);
    CHECK_EQ(v[2].lighting, 3u);                     // Mediterranean without the attribute: Europe's lighting
    CHECK_EQ(v[3].culture, 5u);
    CHECK_EQ(v[3].lighting, 1u);                     // the attribute names Asia
    CHECK_EQ(v[3].source, std::string("test.xml"));
    std::vector<CitySet> r;
    CHECK_OK(parse(culture("name=\"Ruins\"", 4), r));
    CHECK_EQ(r.size(), 4u);
    if (!r.empty()) CHECK_EQ(r[0].lighting, 3u);    // Ruins likewise
    std::vector<CitySet> a;
    CHECK_OK(parse(culture("name=\"Africa\"", 1), a));
    if (!a.empty()) CHECK_EQ(a[0].lighting, 2u);    // its own otherwise
}

C5R_TEST(city_set_file_refusals) {
    std::vector<CitySet> v(1);
    CHECK_ERR(parse("<BuildingArtInfos><Culture name=\"Asia\">", v), Err::BadLayout);            // malformed XML
    CHECK_ERR(parse("<WonderArtInfos/>", v), Err::BadLayout);                                     // another root
    CHECK_ERR(parse(culture("", 1), v), Err::BadLayout);                                          // no name
    CHECK_ERR(parse(culture("name=\"Atlantis\"", 1), v), Err::BadLayout);                         // not one of seven
    CHECK_ERR(parse(culture("name=\"Asia\"", 5), v), Err::BadLayout);                             // five eras
    CHECK_ERR(parse(culture("name=\"Asia\" lighting=\"Moon\"", 1), v), Err::BadLayout);           // bad lighting
    CHECK_ERR(parse("<BuildingArtInfos><Culture name=\"Asia\"><Era name=\"a\"/></Culture></BuildingArtInfos>", v),
              Err::BadLayout);                                                                     // no Granny
    CHECK_ERR(parse("<BuildingArtInfos><Culture name=\"Asia\"><Era><PopulationBuildings><Granny></Granny>"
                    "</PopulationBuildings></Era></Culture></BuildingArtInfos>", v),
              Err::BadLayout);                                                                     // an empty Granny
    CHECK_EQ(v.size(), 1u);                                                                        // out unchanged
}

C5R_TEST(conversions) {
    CHECK_EQ(cultureIndex("AMERICA"), 0);
    CHECK_EQ(cultureIndex("ruins"), 6);
    CHECK_EQ(cultureIndex("Europa"), -1);
    CHECK_EQ(std::string(cultureName(5)), std::string("Polynesian"));
    CHECK_EQ(std::string(cultureName(7)), std::string("?"));
    const int ids[] = {0, 1, 2, 3, 4, 5, 6, 7, -1, 100};
    const uint32_t want[] = {0, 1, 2, 3, 4, 5, 6, 3, 3, 3};
    for (int i = 0; i < 10; ++i) CHECK_EQ(cultureFromArtStyle(ids[i]), want[i]);
    CHECK_EQ(std::string(wallFile(6)), std::string("wall_of_babylon.gr2"));
    CHECK_EQ(std::string(wallFile(7)), std::string(""));
    CHECK_EQ(std::string(greatWallFile(1)), std::string("great_wall.gr2"));
    CHECK_EQ(std::string(greatWallFile(2)), std::string(""));
    CHECK_EQ(std::string(cultureAdjective(6)), std::string(""));
    // Era to slot: VP's database (CityArtType NULL in every row, ArtPrefix only) and a mod's CityArtType.
    std::vector<EraRow> eras = {{"ERA_ANCIENT", "", "ANCIENT"},          {"ERA_CLASSICAL", "", "CLASSICAL"},
                                {"ERA_MEDIEVAL", "", "MEDIEVAL"},        {"ERA_RENAISSANCE", "", "RENAISSANCE"},
                                {"ERA_INDUSTRIAL", "", "INDUSTRIAL"},    {"ERA_MODERN", "", "MODERN"},
                                {"ERA_POSTMODERN", "", "POSTMODERN"},    {"ERA_FUTURE", "", "FUTURE"}};
    std::vector<int> s;
    CHECK_OK(eraSlots(eras, s));
    CHECK(s == std::vector<int>({0, 0, 0, 1, 2, 3, 3, 3}));
    eras[2].cityArtType = "RENAISSANCE";                                   // CityArtType first
    CHECK_OK(eraSlots(eras, s));
    CHECK(s == std::vector<int>({0, 0, 1, 1, 2, 3, 3, 3}));
    std::vector<EraRow> odd = {{"A", "", "STONE"}, {"B", "", "MODERN"}};    // no match up to the first: -1
    CHECK_OK(eraSlots(odd, s));
    CHECK(s == std::vector<int>({-1, 3}));
    CHECK_ERR(eraSlots(std::vector<EraRow>(), s), Err::BadLayout);
    CHECK(s.empty());
}

C5R_TEST(wonder_forms) {
    const std::vector<std::string> types = {"CASTLE",        "POLYNESIAN CASTLE", "ASIAN CASTLE", "HIMEJI CASTLE",
                                            "ANCIENT HARBOR", "MODERN HARBOR",    "STADIUM ONE",  "STADIUM TWO",
                                            "STADIUM FIVE",   "XCASTLE"};
    std::vector<WonderForm> f = wonderForms("CASTLE", types);
    CHECK_EQ(f.size(), 3u);                         // plain, two cultural; HIMEJI is not a culture, XCASTLE no word
    if (f.size() == 3) {
        CHECK(f[0].kind == FormKind::Cultural && f[0].qualifier == "ASIAN");
        CHECK(f[1].kind == FormKind::Plain);
        CHECK(f[2].kind == FormKind::Cultural && f[2].qualifier == "POLYNESIAN");
    }
    f = wonderForms("HARBOR", types);
    CHECK_EQ(f.size(), 2u);
    if (f.size() == 2) CHECK(f[0].kind == FormKind::Era && f[0].qualifier == "ANCIENT");
    f = wonderForms("STADIUM", types);
    CHECK_EQ(f.size(), 2u);                         // ONE, TWO; FIVE is not a random word
    if (!f.empty()) CHECK(f[0].kind == FormKind::Random);
    CHECK(wonderForms("", types).empty());
    CHECK(wonderForms("PYRAMIDS", types).empty());
}

C5R_TEST(catalogue_over_the_install) {
    lmt::Install& in = lmt::install(false);
    CHECK_OK(in.artStatus);
    if (!in.artStatus.ok()) return;
    const ArtCatalogue& a = in.art;
    const CatalogueCounts& c = a.counts();
    std::printf("city-set files %u, sets %u, models %u; wonder types %u (%u entries); world wonders %u; display %u "
                "(%u world wonders, %u without art)\n",
                c.citySetFiles, c.citySets, c.citySetModels, c.wonderArtTypes, c.wonderArtEntries, c.worldWonders,
                c.displayBuildings, c.displayWorldWonders, c.displayWithoutArt);
    CHECK_EQ(c.citySetFiles, 2u);            // the base file and DLC_03's Polynesia file (1.2)
    CHECK_EQ(c.citySets, 28u);
    CHECK_EQ(c.citySetModels, 25u);
    CHECK_EQ(c.wonderArtTypes, 76u);         // model-reader.md 3
    CHECK_EQ(c.worldWonders, 76u);           // scene-catalogue.md 11 (MaxGlobalInstances = 1)
    CHECK_EQ(c.displayBuildings, 58u);       // cities-and-landmarks.md 1.6
    CHECK_EQ(c.displayWorldWonders, 46u);
    const CitySet* poly = a.citySet(5, 0);
    CHECK(poly != nullptr);
    if (poly) {
        CHECK_EQ(poly->granny, std::string("poly_anc_city.gr2"));
        CHECK_EQ(poly->lighting, 1u);
    }
    const CitySet* medit = a.citySet(4, 3);
    CHECK(medit != nullptr);
    if (medit) CHECK_EQ(medit->lighting, 3u);
    for (uint32_t s = 0; s < kEraSlots; ++s) {
        const CitySet* r = a.citySet(kRuins, s);
        CHECK(r != nullptr);
        if (r) CHECK_EQ(r->granny, std::string("city_ruins.gr2"));
    }
    CHECK(a.citySet(7, 0) == nullptr);
    CHECK_EQ(a.eraSlot("ERA_MEDIEVAL"), 0);
    CHECK_EQ(a.eraSlot("ERA_RENAISSANCE"), 1);
    CHECK_EQ(a.eraSlot("ERA_FUTURE"), 3);
    CHECK_EQ(a.eraSlot("ERA_NONE"), -1);
    uint32_t castleForms = 0, harborForms = 0, stadiumForms = 0;
    for (const DisplayBuilding& d : a.displayBuildings()) {
        if (d.building == "BUILDING_CASTLE") castleForms = static_cast<uint32_t>(d.forms.size());
        if (d.building == "BUILDING_HARBOR") harborForms = static_cast<uint32_t>(d.forms.size());
        if (d.building == "BUILDING_STADIUM") stadiumForms = static_cast<uint32_t>(d.forms.size());
        if (d.forms.empty()) std::printf("display building without art: %s (tag %s)\n", d.building.c_str(), d.tag.c_str());
    }
    CHECK_EQ(castleForms, 6u);               // six cultural forms (1.6 shows POLYNESIAN CASTLE)
    CHECK_EQ(harborForms, 4u);               // ANCIENT, MEDIEVAL, INDUSTRIAL, MODERN
    CHECK_EQ(stadiumForms, 2u);              // ONE, TWO
    const c5m::WonderArt* w = a.wonder("STONEHENGE");
    CHECK(w != nullptr);
    if (w) CHECK_EQ(w->scale, 0.05f);
    CHECK(a.wonder("NO SUCH WONDER") == nullptr);
}

C5R_TEST(catalogue_refusals) {
    lmt::Install& in = lmt::install(false);
    ArtCatalogue a;
    c5m::TsvTables none;                     // no Eras table
    CHECK_ERR(a.load(in.files, none), Err::NotFound);
    CHECK(!a.loaded());
    CHECK(a.citySets().empty());
    c5m::TsvTables erasOnly;
    c5m::Error e;
    CHECK(erasOnly.add("Eras", "Type\tCityArtType\tArtPrefix\nERA_ANCIENT\t\\N\tANCIENT\n", &e));
    CHECK_ERR(a.load(in.files, erasOnly), Err::NotFound);   // no BuildingClasses
    CHECK(!a.loaded());
    gamefs::GameFiles noInstall;             // a file set with no city-set file
    CHECK_ERR(a.load(noInstall, in.db), Err::NotFound);
}

C5R_TEST_MAIN
